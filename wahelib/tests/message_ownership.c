#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef INVALID_HANDLE_VALUE
#endif
#include "../wahelib.c"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

typedef struct {
    void *pointer;
    int owner;
} allocation_t;

static allocation_t allocations[64];
static size_t live[2], freed[2];
static int fail_caller_allocation;
static int fail_response_copy;
static char static_reply[] = "Static response";
static _Thread_local char thread_reply[] = "Thread-local response";
static char *retained_reply;
static unsigned char payload[] = {1, 2, 3, 4};
static char payload_reply[128];
static char *selected_reply = "Literal response";

static void *allocate_for(int owner, size_t size)
{
    // Simulate a caller allocation failure without preventing the responder's input copy
    if (owner == 0 && fail_caller_allocation)
        return NULL;

    // Track every allocation so freeing a borrowed pointer or another allocator's block fails
    for (size_t i = 0; i < sizeof(allocations) / sizeof(*allocations); i++)
        if (!allocations[i].pointer)
        {
            void *pointer = malloc(size);
            CHECK(pointer);
            allocations[i] = (allocation_t){pointer, owner};
            live[owner]++;
            return pointer;
        }
    CHECK(0);
    return NULL;
}

static void release_for(int owner, void *pointer)
{
    // Accept the normal no-response case without counting a free
    if (!pointer)
        return;

    // Require an exact allocation base and the matching allocator
    for (size_t i = 0; i < sizeof(allocations) / sizeof(*allocations); i++)
        if (allocations[i].pointer == pointer)
        {
            CHECK(allocations[i].owner == owner);
            allocations[i].pointer = NULL;
            live[owner]--;
            freed[owner]++;
            free(pointer);
            return;
        }
    CHECK(0);
}

static void *caller_malloc(size_t size)
{
    // Allocate through the caller's independent allocator
    return allocate_for(0, size);
}

static void caller_free(void *pointer)
{
    // Release only caller-owned allocations
    release_for(0, pointer);
}

static void *responder_malloc(size_t size)
{
    // Allocate through the responding module's independent allocator
    return allocate_for(1, size);
}

static void responder_free(void *pointer)
{
    // Release only responder-owned allocations
    release_for(1, pointer);
}

static char *respond(char *message)
{
    // Read the borrowed input and return independent module-owned storage
    CHECK(message && !strcmp(message, "Ownership test"));
    // Optionally fail copying the result after an earlier host reply has already been allocated
    if (fail_response_copy)
        fail_caller_allocation = 1;
    return selected_reply;
}

static char *consume(char *message)
{
    // Consume a chain's temporary input without freeing it or keeping its pointer
    CHECK(message && !strcmp(message, static_reply));
    return thread_reply;
}

static void initialize_module(wahe_module_t *module, wahe_group_t *group, int index)
{
    // Configure native test callbacks without loading a library or opening a device
    *module = (wahe_module_t){0};
    module->valid = 1;
    module->type = WAHE_MODULE_NATIVE;
    module->has_host_mem_access = 1;
    module->runner_count = 1;
    module->module_name = index ? "responder" : "caller";
    module->module_id = index;
    module->parent_group = group;
    module->dl_func[WAHE_FUNC_MALLOC] = index ? responder_malloc : caller_malloc;
    module->dl_func[WAHE_FUNC_FREE] = index ? responder_free : caller_free;
    module->dl_func[WAHE_FUNC_INPUT] = respond;
    module->dl_func[WAHE_FUNC_PROC_CMD] = respond;
}

static void check_registered_replies(wahe_group_t *group)
{
    // Exercise literal, static, TLS and retained heap originals without transferring ownership
    retained_reply = responder_malloc(32);
    strcpy(retained_reply, "Retained response");
    char *variants[] = {"Literal response", static_reply, thread_reply, retained_reply};
    for (size_t i = 0; i < sizeof(variants) / sizeof(*variants); i++)
    {
        selected_reply = variants[i];
        size_t previous_freed = freed[1];
        char *first = wahe_run_command_with_id_native(&group->module[0], "Ownership test");
        char *second = wahe_run_command_with_id_native(&group->module[0], "Ownership test");
        CHECK(first && second && first != second && first != selected_reply);
        CHECK(!strcmp(first, selected_reply) && !strcmp(second, selected_reply));
        CHECK(freed[1] == previous_freed + 2 && live[1] == 1 && live[0] == 2);
        caller_free(first);
        caller_free(second);
    }

    // Keep payload ownership separate while copying only its native text descriptor
    snprintf(payload_reply, sizeof(payload_reply), "Data location: %zu bytes at %#zx", sizeof(payload), (size_t)payload);
    selected_reply = payload_reply;
    char *reply = wahe_run_command_with_id_native(&group->module[0], "Ownership test");
    size_t count = 0, address = 0;
    CHECK(reply && sscanf(reply, "Data location: %zu bytes at %zx", &count, &address) == 2);
    CHECK(count == sizeof(payload) && address == (size_t)payload);
    caller_free(reply);
    CHECK(payload[0] == 1 && payload[3] == 4);

    // Preserve the producer's heap response when the caller's response-copy allocation fails
    selected_reply = retained_reply;
    fail_caller_allocation = 1;
    size_t previous_freed = freed[1];
    CHECK(!wahe_run_command_with_id_native(&group->module[0], "Ownership test"));
    CHECK(freed[1] == previous_freed + 1 && live[1] == 1 && live[0] == 0);
    CHECK(!strcmp(retained_reply, "Retained response"));
    fail_caller_allocation = 0;

    // Let only the producing module release its retained response
    responder_free(retained_reply);
    retained_reply = NULL;
}

static void check_processor_and_chain(wahe_group_t *group)
{
    // Route commands through a processor and verify only its temporary input gets freed
    int processor = 1;
    wahe_exec_order_t order = {0};
    order.module_id = 0;
    order.cmd_proc_id = &processor;
    order.cmd_proc_count = 1;
    wahe_chain_t chain = {0};
    chain.parent_group = group;
    chain.exec_order = &order;
    chain.exec_order_count = 1;
    chain.current_eo = 0;
    selected_reply = static_reply;
    wahe_cur_chain = &chain;
    char *reply = wahe_run_command_with_id_native(&group->module[0], "Ownership test");
    wahe_cur_chain = NULL;
    CHECK(reply && reply != static_reply && !strcmp(reply, static_reply));
    CHECK(!live[1] && live[0] == 1 && chain.current_cmd_proc_id == 0);
    caller_free(reply);

    // Copy chain input between independent allocators while keeping both callback results borrowed
    wahe_exec_order_t orders[3] = {0};
    orders[0].type = WAHE_EO_CHAIN_INPUT_MSG;
    orders[1].type = WAHE_EO_MODULE_FUNC;
    orders[1].module_id = 1;
    orders[1].func_id = WAHE_FUNC_INPUT;
    orders[2].type = WAHE_EO_MODULE_FUNC;
    orders[2].module_id = 0;
    orders[2].func_id = WAHE_FUNC_INPUT;
    wahe_connection_t connections[] = {{0, 1}, {1, 2}};
    group->module[0].dl_func[WAHE_FUNC_INPUT] = consume;
    chain.exec_order = orders;
    chain.exec_order_count = 3;
    chain.connection = connections;
    chain.conn_count = 2;
    chain.current_eo = -1;
    reply = wahe_execute_chain(&chain, "Ownership test");
    CHECK(reply == thread_reply && orders[1].ret_msg_addr == (size_t)static_reply);
    CHECK(!orders[1].dst_msg_addr && !orders[2].dst_msg_addr);
    CHECK(!live[0] && !live[1] && wahe_cur_chain == NULL);
    group->module[0].dl_func[WAHE_FUNC_INPUT] = respond;
}

#ifdef _WIN32
static void check_miniaudio(wahe_group_t *group, const char *path)
{
    // Load a real module without calling Init or starting its audio controller
    HMODULE library = LoadLibraryA(path);
    CHECK(library);
    wahe_module_t *module = &group->module[1];
    module->dl_func[WAHE_FUNC_MALLOC] = (void *)GetProcAddress(library, "module_malloc");
    module->dl_func[WAHE_FUNC_FREE] = (void *)GetProcAddress(library, "module_free");
    module->dl_func[WAHE_FUNC_INPUT] = (void *)GetProcAddress(library, "module_message_input");
    CHECK(module->dl_func[WAHE_FUNC_MALLOC] && module->dl_func[WAHE_FUNC_FREE] && module->dl_func[WAHE_FUNC_INPUT]);
    wahe_register_commands(module, "Audio sample rate");

    // Trigger a borrowed literal response and release only the caller-owned copy
    char *reply = wahe_run_command_with_id_native(&group->module[0], "Audio sample rate 0");
    CHECK(reply && strstr(reply, "Audio sample rate"));
    caller_free(reply);
    CHECK(!live[0]);
    CHECK(FreeLibrary(library));
}
#endif

int main(int argc, char **argv)
{
    // Set up two module contexts with deliberately incompatible allocation tracking
    wahe_module_t modules[2];
    wahe_group_t group = {0};
    group.module = modules;
    group.module_count = 2;
    initialize_module(&modules[0], &group, 0);
    initialize_module(&modules[1], &group, 1);
    wahe_register_commands(&modules[1], "Ownership test");

    // Distinguish native absolute pointers from linear-memory offsets when formatting host replies
    unsigned char dummy_memory[32];
    modules[0].memory_ptr = dummy_memory;
    char *reply = (char *)module_sprintf_alloc(&modules[0], "Value %d", 42);
    CHECK(reply && !strcmp(reply, "Value 42"));
    caller_free(reply);
    reply = wahe_run_command_with_id_native(&modules[0], "Get raw time");
    CHECK(reply && !strncmp(reply, "Raw time ", 9));
    caller_free(reply);
    modules[0].memory_ptr = NULL;

    // Keep only the final owned reply when a single command message produces multiple responses
    reply = wahe_run_command_with_id_native(&modules[0], "Get raw time\nGet raw time");
    CHECK(reply && live[0] == 1);
    caller_free(reply);

    // Replace an earlier owned host reply with a copied borrowed module result
    selected_reply = static_reply;
    reply = wahe_run_command_with_id_native(&modules[0], "Get raw time\nOwnership test");
    CHECK(reply && !strcmp(reply, static_reply) && live[0] == 1 && !live[1]);
    caller_free(reply);

    // Release the superseded host reply even when copying the module result fails
    fail_response_copy = 1;
    CHECK(!wahe_run_command_with_id_native(&modules[0], "Get raw time\nOwnership test"));
    CHECK(!live[0] && !live[1]);
    fail_response_copy = fail_caller_allocation = 0;

    // Treat Init memory as a reservation rather than a caller-allocator text response
    reply = wahe_run_command_with_id_native(&modules[0], "Get raw time\nInit memory to 4096 - 65536");
    CHECK(reply && reply == (char *)modules[0].memory_ptr && !live[0]);
    CHECK(wahe_virtual_memory_free(reply, modules[0].memory_reserve_size));
    modules[0].memory_ptr = NULL;
    modules[0].memory_size = modules[0].memory_reserve_size = 0;

    // Verify that direct host callbacks return borrowed storage after releasing their input
    selected_reply = static_reply;
    CHECK(wahe_send_input(&modules[1], "Ownership %s", "test") == static_reply);
    CHECK(!live[1]);
    check_registered_replies(&group);
    check_processor_and_chain(&group);

    // Optionally exercise the installed native audio module without GUI or device activity
    #ifdef _WIN32
    if (argc > 1)
        check_miniaudio(&group, argv[1]);
    #else
    (void)argc;
    (void)argv;
    #endif

    // Release host-side registry resources and require no outstanding module allocations
    CHECK(!live[0] && !live[1]);
    free(group.cmd_reg);
    rl_mutex_destroy(&group.cmd_reg_mutex);
    puts("Message ownership tests passed (two allocation-failure diagnostics are expected)");
    return 0;
}
