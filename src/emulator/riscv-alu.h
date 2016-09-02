//
//  riscv-alu.h
//

#ifndef riscv_alu_h
#define riscv_alu_h

namespace riscv {

	/* multiply high unsigned unsigned */

	template <typename U>
	U mulhu(U x, U y)
	{
		const int qb = sizeof(U) << 2;
		const U   mask = (U(1) << qb) - 1;

		U x0 =    x       & mask;
		U x1 =    x >> qb & mask;
		U y0 =    y       & mask;
		U y1 =    y >> qb & mask;
		U z0 =    x0 * y0;
		U z1 =    x1 * y0;
		U z2 =    x0 * y1;
		U z3 =    x1 * y1;
		U z4 =    z1 + z2;
		U c1 =    z4 < z1;
		U lo =    z0 + (z4 << qb);
		U c2 =    lo < z0;
		U hi =    z3 + (z4 >> qb) + (c1 << qb) + c2;

		return hi;
	}

	/* multiply high signed signed */

	template <typename S>
	S mulh(S x, S y)
	{
		typedef typename std::make_unsigned<S>::type U;

		const int qb = sizeof(U) << 2;
		const U   mask = (U(1) << qb) - 1;
		const int sshift = (sizeof(U) << 3) - 1;

		U xs =    U(x) >> sshift;
		U ys =    U(y) >> sshift;
		U xu =    xs ? -x : x;
		U x0 =    xu       & mask;
		U x1 =    xu >> qb & mask;
		U yu =    ys ? -y : y;
		U y0 =    yu       & mask;
		U y1 =    yu >> qb & mask;
		U z0 =    x0 * y0;
		U z1 =    x1 * y0;
		U z2 =    x0 * y1;
		U z3 =    x1 * y1;
		U z4 =    z1 + z2;
		U c1 =    z4 < z1;
		U l1 =    z0 + (z4 << qb);
		U c2 =    l1 < z0;
		U rs =    xs ^ ys;
		U h1 =    z3 + (z4 >> qb) + (c1 << qb) + c2;
		U l2 =    l1 - rs;
		U hb =    l2 > l1;
		U h2 =    h1 - hb;
		U hi =    -rs ^ h2;

		return hi;
	}

	/* multiply high signed unsigned */

	template <typename S, typename U>
	S mulhsu(S x, U y)
	{
		const int qb = sizeof(U) << 2;
		const U   mask = (U(1) << qb) - 1;
		const int sshift = (sizeof(U) << 3) - 1;

		U xs =    U(x) >> sshift;
		U xu =    xs ? -x : x;
		U x0 =    xu       & mask;
		U x1 =    xu >> qb & mask;
		U y0 =    y        & mask;
		U y1 =    y >> qb  & mask;
		U z0 =    x0 * y0;
		U z1 =    x1 * y0;
		U z2 =    x0 * y1;
		U z3 =    x1 * y1;
		U z4 =    z1 + z2;
		U c1 =    z4 < z1;
		U l1 =    z0 + (z4 << qb);
		U c2 =    l1 < z0;
		U h1 =    z3 + (z4 >> qb) + (c1 << qb) + c2;
		U l2 =    l1 - xs;
		U hb =    l2 > l1;
		U h2 =    h1 - hb;
		U hi =    -xs ^ h2;

		return hi;
	}

}

#endif