typedef union
{
	 int8_t  i8[16];
	uint8_t  u8[16];
	 int16_t i16[8];
	uint16_t u16[8];
	 int32_t i32[4];
	uint32_t u32[4];
	 int64_t i64[2];
	uint64_t u64[2];
	float    f32[4];
	double   f64[2];
} v128_t;

#define uint128_t v128_t

// Reference: https://doc.rust-lang.org/src/core/stdarch/crates/core_arch/src/wasm32/simd128.rs.html

// Load
v128_t load128_align0(const uint8_t  *ptr) { v128_t val; memcpy(&val, ptr, sizeof(val)); return val; }
v128_t load128_align1(const uint16_t *ptr) { v128_t val; memcpy(&val, ptr, sizeof(val)); return val; }
v128_t load128_align2(const uint32_t *ptr) { v128_t val; memcpy(&val, ptr, sizeof(val)); return val; }
v128_t load128_align3(const uint64_t *ptr) { v128_t val; memcpy(&val, ptr, sizeof(val)); return val; }
v128_t load128_align4(const v128_t   *ptr) { v128_t val; memcpy(&val, ptr, sizeof(val)); return val; }

v128_t simd_v128_load8x8_s(const    int8_t *ptr) { v128_t r; for (int i=0; i<8; i++) r.i16[i] = ptr[i]; return r; }
v128_t simd_v128_load8x8_u(const   uint8_t *ptr) { v128_t r; for (int i=0; i<8; i++) r.i16[i] = ptr[i]; return r; }
v128_t simd_v128_load16x4_s(const  int16_t *ptr) { v128_t r; for (int i=0; i<4; i++) r.i32[i] = ptr[i]; return r; }
v128_t simd_v128_load16x4_u(const uint16_t *ptr) { v128_t r; for (int i=0; i<4; i++) r.i32[i] = ptr[i]; return r; }
v128_t simd_v128_load32x2_s(const  int32_t *ptr) { v128_t r; for (int i=0; i<2; i++) r.i64[i] = ptr[i]; return r; }
v128_t simd_v128_load32x2_u(const uint32_t *ptr) { v128_t r; for (int i=0; i<2; i++) r.i64[i] = ptr[i]; return r; }
v128_t simd_v128_load8_splat(const   uint8_t *ptr) { v128_t r; for (int i=0; i<16; i++) r.u8[i] = *ptr; return r; }
v128_t simd_v128_load16_splat(const uint16_t *ptr) { v128_t r; for (int i=0; i<8; i++) r.u16[i] = *ptr; return r; }
v128_t simd_v128_load32_splat(const uint32_t *ptr) { v128_t r; for (int i=0; i<4; i++) r.u32[i] = *ptr; return r; }
v128_t simd_v128_load64_splat(const uint64_t *ptr) { v128_t r; for (int i=0; i<2; i++) r.u64[i] = *ptr; return r; }

// Store
void store128_align0(uint8_t  *ptr, v128_t val) { memcpy(ptr, &val, sizeof(val)); }
void store128_align1(uint16_t *ptr, v128_t val) { memcpy(ptr, &val, sizeof(val)); }
void store128_align2(uint32_t *ptr, v128_t val) { memcpy(ptr, &val, sizeof(val)); }
void store128_align3(uint64_t *ptr, v128_t val) { memcpy(ptr, &val, sizeof(val)); }
void store128_align4(v128_t   *ptr, v128_t val) { memcpy(ptr, &val, sizeof(val)); }

// Const
v128_t V128_C(uint64_t a, uint64_t b) { v128_t r; r.u64[0] = a; r.u64[1] = b; return r; }

// Lane selection
v128_t simd_i8x16_shuffle(v128_t a, v128_t b, const int8_t *s) { v128_t r; for (int i=0; i<16; i++) r.u8[i] = s[i] < 16 ? a.u8[s[i]] : b.u8[s[i]-16]; return r; }
v128_t simd_i8x16_swizzle(v128_t a, v128_t b, const int8_t *s) { v128_t r; for (int i=0; i<16; i++) r.u8[i] = s[i] < 16 ? a.u8[s[i]] : 0; return r; }

// Splat
v128_t simd_i8x16_splat(uint8_t a)  { v128_t r; for (int i=0; i<16; i++) r.u16[i] = a; return r; }
v128_t simd_i16x8_splat(uint16_t a) { v128_t r; for (int i=0; i<8;  i++) r.u16[i] = a; return r; }
v128_t simd_i32x4_splat(uint32_t a) { v128_t r; for (int i=0; i<4;  i++) r.u32[i] = a; return r; }
v128_t simd_i64x2_splat(uint64_t a) { v128_t r; for (int i=0; i<2;  i++) r.u64[i] = a; return r; }
v128_t simd_f32x4_splat(float a)    { v128_t r; for (int i=0; i<4;  i++) r.f32[i] = a; return r; }
v128_t simd_f64x2_splat(double a)   { v128_t r; for (int i=0; i<2;  i++) r.f64[i] = a; return r; }

// Extract lane + replace lane
int32_t simd_i8x16_extract_lane_s(v128_t a, int lane) { return a.i8[lane]; }
int32_t simd_i8x16_extract_lane_u(v128_t a, int lane) { return a.u8[lane]; }
int32_t simd_i16x8_extract_lane_s(v128_t a, int lane) { return a.i16[lane]; }
int32_t simd_i16x8_extract_lane_u(v128_t a, int lane) { return a.u16[lane]; }
int32_t simd_i32x4_extract_lane(v128_t a, int lane) { return a.i32[lane]; }
int64_t simd_i64x2_extract_lane(v128_t a, int lane) { return a.i64[lane]; }
float  simd_f32x4_extract_lane(v128_t a, int lane) { return a.f32[lane]; }
double simd_f64x2_extract_lane(v128_t a, int lane) { return a.f64[lane]; }

v128_t simd_i8x16_replace_lane(v128_t a, int lane, int8_t  s) { a.i8[lane]  = s; return a; }
v128_t simd_i16x8_replace_lane(v128_t a, int lane, int16_t s) { a.i16[lane] = s; return a; }
v128_t simd_i32x4_replace_lane(v128_t a, int lane, int32_t s) { a.i32[lane] = s; return a; }
v128_t simd_i64x2_replace_lane(v128_t a, int lane, int64_t s) { a.i64[lane] = s; return a; }
v128_t simd_f32x4_replace_lane(v128_t a, int lane, float   s) { a.f32[lane] = s; return a; }
v128_t simd_f64x2_replace_lane(v128_t a, int lane, double  s) { a.f64[lane] = s; return a; }

// Comparisons
v128_t simd_i8x16_eq  (v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.u8[i] == b.u8[i]); return a; }
v128_t simd_i8x16_ne  (v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.u8[i] != b.u8[i]); return a; }
v128_t simd_i8x16_lt_s(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.i8[i]  < b.i8[i]); return a; }
v128_t simd_i8x16_lt_u(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.u8[i]  < b.u8[i]); return a; }
v128_t simd_i8x16_gt_s(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.i8[i]  > b.i8[i]); return a; }
v128_t simd_i8x16_gt_u(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.u8[i]  > b.u8[i]); return a; }
v128_t simd_i8x16_le_s(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.i8[i] <= b.i8[i]); return a; }
v128_t simd_i8x16_le_u(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.u8[i] <= b.u8[i]); return a; }
v128_t simd_i8x16_ge_s(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.i8[i] >= b.i8[i]); return a; }
v128_t simd_i8x16_ge_u(v128_t a, v128_t b) { for (int i=0; i<16; i++) a.i8[i] = -(a.u8[i] >= b.u8[i]); return a; }

v128_t simd_i16x8_eq  (v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.u16[i] == b.u16[i]); return a; }
v128_t simd_i16x8_ne  (v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.u16[i] != b.u16[i]); return a; }
v128_t simd_i16x8_lt_s(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.i16[i]  < b.i16[i]); return a; }
v128_t simd_i16x8_lt_u(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.u16[i]  < b.u16[i]); return a; }
v128_t simd_i16x8_gt_s(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.i16[i]  > b.i16[i]); return a; }
v128_t simd_i16x8_gt_u(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.u16[i]  > b.u16[i]); return a; }
v128_t simd_i16x8_le_s(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.i16[i] <= b.i16[i]); return a; }
v128_t simd_i16x8_le_u(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.u16[i] <= b.u16[i]); return a; }
v128_t simd_i16x8_ge_s(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.i16[i] >= b.i16[i]); return a; }
v128_t simd_i16x8_ge_u(v128_t a, v128_t b) { for (int i=0; i<8; i++) a.i16[i] = -(a.u16[i] >= b.u16[i]); return a; }

v128_t simd_i32x4_eq  (v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.u32[i] == b.u32[i]); return a; }
v128_t simd_i32x4_ne  (v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.u32[i] != b.u32[i]); return a; }
v128_t simd_i32x4_lt_s(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.i32[i]  < b.i32[i]); return a; }
v128_t simd_i32x4_lt_u(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.u32[i]  < b.u32[i]); return a; }
v128_t simd_i32x4_gt_s(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.i32[i]  > b.i32[i]); return a; }
v128_t simd_i32x4_gt_u(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.u32[i]  > b.u32[i]); return a; }
v128_t simd_i32x4_le_s(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.i32[i] <= b.i32[i]); return a; }
v128_t simd_i32x4_le_u(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.u32[i] <= b.u32[i]); return a; }
v128_t simd_i32x4_ge_s(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.i32[i] >= b.i32[i]); return a; }
v128_t simd_i32x4_ge_u(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.u32[i] >= b.u32[i]); return a; }

v128_t simd_f32x4_eq(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.f32[i] == b.f32[i]); return a; }
v128_t simd_f32x4_ne(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.f32[i] != b.f32[i]); return a; }
v128_t simd_f32x4_lt(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.f32[i]  < b.f32[i]); return a; }
v128_t simd_f32x4_gt(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.f32[i]  > b.f32[i]); return a; }
v128_t simd_f32x4_le(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.f32[i] <= b.f32[i]); return a; }
v128_t simd_f32x4_ge(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] = -(a.f32[i] >= b.f32[i]); return a; }

v128_t simd_f64x2_eq(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.i64[i] = -(a.f64[i] == b.f64[i]); return a; }
v128_t simd_f64x2_ne(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.i64[i] = -(a.f64[i] != b.f64[i]); return a; }
v128_t simd_f64x2_lt(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.i64[i] = -(a.f64[i]  < b.f64[i]); return a; }
v128_t simd_f64x2_gt(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.i64[i] = -(a.f64[i]  > b.f64[i]); return a; }
v128_t simd_f64x2_le(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.i64[i] = -(a.f64[i] <= b.f64[i]); return a; }
v128_t simd_f64x2_ge(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.i64[i] = -(a.f64[i] >= b.f64[i]); return a; }

// Bitwise operations
v128_t simd_v128_not(v128_t a)              { for (int i=0; i<2; i++) a.u64[i] = ~a.u64[i]; return a; }
v128_t simd_v128_and(v128_t a, v128_t b)    { for (int i=0; i<2; i++) a.u64[i] &= b.u64[i]; return a; }
v128_t simd_v128_andnot(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.u64[i] &= ~b.u64[i]; return a; }
v128_t simd_v128_or (v128_t a, v128_t b)    { for (int i=0; i<2; i++) a.u64[i] |= b.u64[i]; return a; }
v128_t simd_v128_xor(v128_t a, v128_t b)    { for (int i=0; i<2; i++) a.u64[i] ^= b.u64[i]; return a; }
v128_t simd_v128_bitselect(v128_t a, v128_t b, v128_t c) { for (int i=0; i<2; i++) a.u64[i] = a.u64[i] & c.u64[i] | b.u64[i] & ~c.u64[i]; return a; }
int32_t simd_v128_any_true(v128_t a) { return a.u64[0] != 0 | a.u64[1] != 0; }

/* TODO
simd_v128_load8_lane
simd_v128_load16_lane
simd_v128_load32_lane
simd_v128_load64_lane
simd_v128_store8_lane
simd_v128_store16_lane
simd_v128_store32_lane
simd_v128_store64_lane
*/
v128_t simd_v128_load32_zero(const uint32_t *ptr) { v128_t r={0}; r.u32[0] = *ptr; return r; }
v128_t simd_v128_load64_zero(const uint64_t *ptr) { v128_t r={0}; r.u64[0] = *ptr; return r; }
/* TODO
simd_f32x4_demote_f64x2_zero
simd_f64x2_promote_low_f32x4
*/

// Integer math operations
int popcount(int x) { int v = 0; while (x != 0) { x &= x - 1; v++; } return v; }
v128_t simd_i8x16_abs(v128_t a)    { for (int i=0; i<16; i++) a.i8[i] = abs(a.i8[i]); return a; }
v128_t simd_i8x16_neg(v128_t a)    { for (int i=0; i<16; i++) a.i8[i] = -a.i8[i]; return a; }
v128_t simd_i8x16_popcnt(v128_t a) { for (int i=0; i<16; i++) a.i8[i] = popcount(a.u8[i]); return a; }
int32_t simd_i8x16_all_true(v128_t a) { for (int i=0; i<16; i++) if (a.u8[i] == 0) return 0; return 1; }
int32_t simd_i8x16_bitmask(v128_t a) { int r=0; for (int i=0; i<16; i++) if (a.i8[i] < 0) r |= (1 << i); return r; }
/* TODO
simd_i8x16_narrow_i16x8_s
simd_i8x16_narrow_i16x8_u
simd_i8x16_shl
simd_i8x16_shr_s
simd_i8x16_shr_u
simd_i8x16_add
simd_i8x16_add_sat_s
simd_i8x16_add_sat_u
simd_i8x16_sub
simd_i8x16_sub_sat_s
simd_i8x16_sub_sat_u
simd_i8x16_min_s
simd_i8x16_min_u
simd_i8x16_max_s
simd_i8x16_max_u
simd_i8x16_avgr_u
simd_i16x8_extadd_pairwise_i8x16_s
simd_i16x8_extadd_pairwise_i8x16_u
simd_i32x4_extadd_pairwise_i16x8_s
simd_i32x4_extadd_pairwise_i16x8_u

simd_i16x8_abs
simd_i16x8_neg
simd_i16x8_q15mulr_sat_s
simd_i16x8_all_true
simd_i16x8_bitmask
simd_i16x8_narrow_i32x4_s
simd_i16x8_narrow_i32x4_u
simd_i16x8_extend_low_i8x16_s
simd_i16x8_extend_high_i8x16_s
simd_i16x8_extend_low_i8x16_u
simd_i16x8_extend_high_i8x16_u
simd_i16x8_shl
simd_i16x8_shr_s
simd_i16x8_shr_u
simd_i16x8_add
simd_i16x8_add_sat_s
simd_i16x8_add_sat_u
simd_i16x8_sub
simd_i16x8_sub_sat_s
simd_i16x8_sub_sat_u
simd_i16x8_mul
simd_i16x8_min_s
simd_i16x8_min_u
simd_i16x8_max_s
simd_i16x8_max_u
simd_i16x8_avgr_u
simd_i16x8_extmul_low_i8x16_s
simd_i16x8_extmul_high_i8x16_s
simd_i16x8_extmul_low_i8x16_u
simd_i16x8_extmul_high_i8x16_u
*/
v128_t simd_i32x4_abs(v128_t a) { for (int i=0; i<4; i++) a.i32[i] = abs(a.i32[i]); return a; }
v128_t simd_i32x4_neg(v128_t a) { for (int i=0; i<4; i++) a.i32[i] = -a.i32[i]; return a; }
/* TODO
simd_i32x4_all_true
simd_i32x4_bitmask
simd_i32x4_extend_low_i16x8_s
simd_i32x4_extend_high_i16x8_s
simd_i32x4_extend_low_i16x8_u
simd_i32x4_extend_high_i16x8_u
*/
v128_t simd_i32x4_shl(v128_t a, uint32_t s)   { s &= 31; for (int i=0; i<4; i++) a.u32[i] <<= s; return a; }
v128_t simd_i32x4_shr_s(v128_t a, uint32_t s) { s &= 31; for (int i=0; i<4; i++) a.i32[i] >>= s; return a; }
v128_t simd_i32x4_shr_u(v128_t a, uint32_t s) { s &= 31; for (int i=0; i<4; i++) a.u32[i] >>= s; return a; }
v128_t simd_i32x4_add(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] += b.i32[i]; return a; }
v128_t simd_i32x4_sub(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] -= b.i32[i]; return a; }
v128_t simd_i32x4_mul(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.i32[i] *= b.i32[i]; return a; }
v128_t simd_i32x4_min_s(v128_t a, v128_t b) { for (int i=0; i<4; i++) if (a.i32[i] > b.i32[i]) a.i32[i] = b.i32[i]; return a; }
v128_t simd_i32x4_min_u(v128_t a, v128_t b) { for (int i=0; i<4; i++) if (a.u32[i] > b.u32[i]) a.u32[i] = b.u32[i]; return a; }
v128_t simd_i32x4_max_s(v128_t a, v128_t b) { for (int i=0; i<4; i++) if (a.i32[i] < b.i32[i]) a.i32[i] = b.i32[i]; return a; }
v128_t simd_i32x4_max_u(v128_t a, v128_t b) { for (int i=0; i<4; i++) if (a.u32[i] < b.u32[i]) a.u32[i] = b.u32[i]; return a; }

/* TODO
simd_i32x4_dot_i16x8_s
simd_i32x4_extmul_low_i16x8_s
simd_i32x4_extmul_high_i16x8_s
simd_i32x4_extmul_low_i16x8_u
simd_i32x4_extmul_high_i16x8_u

simd_i64x2_abs
simd_i64x2_neg
simd_i64x2_all_true
simd_i64x2_bitmask
*/
v128_t simd_i64x2_extend_low_i32x4_s(v128_t a)  { for (int i=1; i>=0; i--) a.i64[i] = a.i32[i]; return a; }
v128_t simd_i64x2_extend_high_i32x4_s(v128_t a) { for (int i=1; i>=0; i--) a.i64[i] = a.i32[i+2]; return a; }
v128_t simd_i64x2_extend_low_i32x4_u(v128_t a)  { for (int i=1; i>=0; i--) a.u64[i] = a.u32[i]; return a; }
v128_t simd_i64x2_extend_high_i32x4_u(v128_t a) { for (int i=1; i>=0; i--) a.u64[i] = a.u32[i+2]; return a; }

v128_t simd_i64x2_shl(v128_t a, uint32_t s)   { s &= 63; for (int i=0; i<2; i++) a.u64[i] <<= s; return a; }
v128_t simd_i64x2_shr_s(v128_t a, uint32_t s) { s &= 63; for (int i=0; i<2; i++) a.i64[i] >>= s; return a; }
v128_t simd_i64x2_shr_u(v128_t a, uint32_t s) { s &= 63; for (int i=0; i<2; i++) a.u64[i] >>= s; return a; }
/* TODO
simd_i64x2_add
simd_i64x2_sub
simd_i64x2_mul
simd_i64x2_eq
simd_i64x2_ne
simd_i64x2_lt_s
simd_i64x2_gt_s
simd_i64x2_le_s
simd_i64x2_ge_s
simd_i64x2_extmul_low_i32x4_s
simd_i64x2_extmul_high_i32x4_s
simd_i64x2_extmul_low_i32x4_u
simd_i64x2_extmul_high_i32x4_u
*/

// Float math operations
v128_t simd_f32x4_ceil(v128_t a)    { for (int i=0; i<4; i++) a.f32[i] = ceilf(a.f32[i]); return a; }
v128_t simd_f32x4_floor(v128_t a)   { for (int i=0; i<4; i++) a.f32[i] = floorf(a.f32[i]); return a; }
v128_t simd_f32x4_trunc(v128_t a)   { for (int i=0; i<4; i++) a.f32[i] = truncf(a.f32[i]); return a; }
v128_t simd_f32x4_nearest(v128_t a) { for (int i=0; i<4; i++) a.f32[i] = nearbyintf(a.f32[i]); return a; }
v128_t simd_f64x2_ceil(v128_t a)    { for (int i=0; i<2; i++) a.f64[i] = ceil(a.f64[i]); return a; }
v128_t simd_f64x2_floor(v128_t a)   { for (int i=0; i<2; i++) a.f64[i] = floor(a.f64[i]); return a; }
v128_t simd_f64x2_trunc(v128_t a)   { for (int i=0; i<2; i++) a.f64[i] = trunc(a.f64[i]); return a; }
v128_t simd_f64x2_nearest(v128_t a) { for (int i=0; i<2; i++) a.f64[i] = nearbyint(a.f64[i]); return a; }

v128_t simd_f32x4_abs(v128_t a)  { for (int i=0; i<4; i++) a.f32[i] = fabsf(a.f32[i]); return a; }
v128_t simd_f32x4_neg(v128_t a)  { for (int i=0; i<4; i++) a.f32[i] = -a.f32[i];      return a; }
v128_t simd_f32x4_sqrt(v128_t a) { for (int i=0; i<4; i++) a.f32[i] = sqrtf(a.f32[i]); return a; }
v128_t simd_f32x4_add(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.f32[i] += b.f32[i]; return a; }
v128_t simd_f32x4_sub(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.f32[i] -= b.f32[i]; return a; }
v128_t simd_f32x4_mul(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.f32[i] *= b.f32[i]; return a; }
v128_t simd_f32x4_div(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.f32[i] /= b.f32[i]; return a; }
// TODO simd_f32x4_min
// TODO simd_f32x4_max
v128_t simd_f32x4_pmin(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.f32[i] = b.f32[i] < a.f32[i] ? b.f32[i] : a.f32[i]; return a; }
v128_t simd_f32x4_pmax(v128_t a, v128_t b) { for (int i=0; i<4; i++) a.f32[i] = b.f32[i] > a.f32[i] ? b.f32[i] : a.f32[i]; return a; }

v128_t simd_f64x2_abs(v128_t a)  { for (int i=0; i<2; i++) a.f64[i] = fabs(a.f64[i]); return a; }
v128_t simd_f64x2_neg(v128_t a)  { for (int i=0; i<2; i++) a.f64[i] = -a.f64[i];      return a; }
v128_t simd_f64x2_sqrt(v128_t a) { for (int i=0; i<2; i++) a.f64[i] = sqrt(a.f64[i]); return a; }
v128_t simd_f64x2_add(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.f64[i] += b.f64[i]; return a; }
v128_t simd_f64x2_sub(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.f64[i] -= b.f64[i]; return a; }
v128_t simd_f64x2_mul(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.f64[i] *= b.f64[i]; return a; }
v128_t simd_f64x2_div(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.f64[i] /= b.f64[i]; return a; }
// TODO simd_f64x2_min
// TODO simd_f64x2_max
v128_t simd_f64x2_pmin(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.f64[i] = b.f64[i] < a.f64[i] ? b.f64[i] : a.f64[i]; return a; }
v128_t simd_f64x2_pmax(v128_t a, v128_t b) { for (int i=0; i<2; i++) a.f64[i] = b.f64[i] > a.f64[i] ? b.f64[i] : a.f64[i]; return a; }

// Conversions
/* TODO
simd_i32x4_trunc_sat_f32x4_s
simd_i32x4_trunc_sat_f32x4_u
*/
v128_t simd_f32x4_convert_i32x4_s(v128_t a) { for (int i=0; i<4; i++) a.f32[i] = a.i32[i]; return a; }
v128_t simd_f32x4_convert_i32x4_u(v128_t a) { for (int i=0; i<4; i++) a.f32[i] = a.u32[i]; return a; }
/* TODO
simd_i32x4_trunc_sat_f64x2_s_zero
simd_i32x4_trunc_sat_f64x2_u_zero
*/
v128_t simd_f64x2_convert_low_i32x4_s(v128_t a) { for (int i=0; i<2; i++) a.f64[i] = a.i32[i]; return a; }
v128_t simd_f64x2_convert_low_i32x4_u(v128_t a) { for (int i=0; i<2; i++) a.f64[i] = a.u32[i]; return a; }

/* TODO
simd_i8x16_relaxed_swizzle
simd_i32x4_relaxed_trunc_f32x4_s
simd_i32x4_relaxed_trunc_f32x4_u
simd_i32x4_relaxed_trunc_f64x2_s_zero
simd_i32x4_relaxed_trunc_f64x2_u_zero*/
v128_t simd_f32x4_relaxed_madd (v128_t x, v128_t y, v128_t z) { for (int i=0; i<4; i++) x.f32[i] = fma( x.f32[i], y.f32[i], z.f32[i]); return x; }
v128_t simd_f32x4_relaxed_nmadd(v128_t x, v128_t y, v128_t z) { for (int i=0; i<4; i++) x.f64[i] = fma(-x.f32[i], y.f32[i], z.f32[i]); return x; }
v128_t simd_f64x2_relaxed_madd (v128_t x, v128_t y, v128_t z) { for (int i=0; i<2; i++) x.f64[i] = fma( x.f64[i], y.f64[i], z.f64[i]); return x; }
v128_t simd_f64x2_relaxed_nmadd(v128_t x, v128_t y, v128_t z) { for (int i=0; i<2; i++) x.f64[i] = fma(-x.f64[i], y.f64[i], z.f64[i]); return x; }
/*simd_i8x16_relaxed_laneselect
simd_i16x8_relaxed_laneselect
simd_i32x4_relaxed_laneselect
simd_i64x2_relaxed_laneselect
simd_f32x4_relaxed_min
simd_f32x4_relaxed_max
simd_f64x2_relaxed_min
simd_f64x2_relaxed_max
simd_i16x8_relaxed_q15mulr_s
simd_i16x8_relaxed_dot_i8x16_i7x16_s
simd_i32x4_relaxed_dot_i8x16_i7x16_add_s
simd_f32x4_relaxed_dot_bf16x8_add_f32x4
*/
