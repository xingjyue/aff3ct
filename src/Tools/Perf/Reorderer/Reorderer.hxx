#include <cmath>
#include <cassert>

#include "Reorderer.hpp"

constexpr bool is_power_of_2(unsigned x)
{
	return (x > 0) && !(x & (x - 1));
}

template <typename T>
void Reorderer<T>
::apply(const std::vector<const T*> in_data, T* out_data, const int data_length)
{
	const int n_fra   = (int)in_data.size();
	const int n_fra_2 = n_fra / 2;

	int start_seq_loop = 0;

	if (is_power_of_2((int)in_data.size()) && (int)in_data.size() >= 2 && (int)in_data.size() <= mipp::nElReg<T>())
	{
		mipp::reg regs_inter[mipp::nElReg<T>()];

		// vectorized reordering
		const auto loop_size = (int)(data_length/mipp::nElReg<T>());
		for (auto i = 0; i < loop_size; i++)
		{
			// loads
			for (auto f = 0; f < n_fra; f++)
				regs_inter[f] = mipp::loadu<T>(in_data[f] + i*mipp::nElReg<T>());

			// reordering
			auto k_size = 1;
			auto l_size = n_fra_2;
			for (auto j = 1; j < n_fra; j <<= 1)
			{
				for (auto k = 0; k < k_size; k++)
				{
					const auto jump = k * l_size * 2;
					for (auto l = 0; l < l_size; l++)
					{
						const auto interx2 = mipp::interleave<T>(regs_inter[jump         +l], 
						                                         regs_inter[jump +l_size +l]);
						regs_inter[jump         +l] = interx2.val[0];
						regs_inter[jump +l_size +l] = interx2.val[1];
					}
				}
				k_size <<= 1;
				l_size >>= 1;
			}

			// stores
			for (auto f = 0; f < n_fra; f++)
				mipp::storeu<T>(out_data + (i*n_fra +f) * mipp::nElReg<T>(), regs_inter[f]);
		}

		start_seq_loop = loop_size * mipp::nElReg<T>();
	}

	// sequential reordering
	for (auto i = start_seq_loop; i < data_length; i++)
		for (auto f = 0; f < n_fra; f++)
			out_data[i*n_fra +f] = in_data[f][i];
}

template <typename T, int N_FRAMES, int J, int K>
struct Reorderer_static_core
{
	static inline void compute(mipp::reg regs_inter[N_FRAMES])
	{
		constexpr auto J_2 = J/2;
		for (auto k = 0; k < K; k++)
		{
			for (auto l = 0; l < J_2; l++)
			{
#ifdef __AVX__
				const auto interx2 = mipp::interleave2<T>(regs_inter[k*J +l], regs_inter[k*J +J_2 +l]);
#else
				const auto interx2 = mipp::interleave <T>(regs_inter[k*J +l], regs_inter[k*J +J_2 +l]);
#endif
				regs_inter[k*J      +l] = interx2.val[0];
				regs_inter[k*J +J_2 +l] = interx2.val[1];
			}
		}

		constexpr auto NEXT_J = J >> 1;
		constexpr auto NEXT_K = K << 1;

		Reorderer_static_core<T,N_FRAMES,NEXT_J,NEXT_K>::compute(regs_inter);
	}
};

#ifdef __AVX__
template <typename T, int N_FRAMES, int K>
struct Reorderer_static_core <T,N_FRAMES,2,K>
{
	static inline void compute(mipp::reg regs_inter[N_FRAMES])
	{
		constexpr auto J = 2;
		constexpr auto J_2 = J/2;
		for (auto k = 0; k < K; k++)
		{
			const auto interx2 = mipp::interleave<T>(regs_inter[k*J], regs_inter[k*J +J_2]);
			regs_inter[k*J     ] = interx2.val[0];
			regs_inter[k*J +J_2] = interx2.val[1];
		}
	}
};
#endif

template <typename T, int N_FRAMES, int K>
struct Reorderer_static_core <T,N_FRAMES,1,K>
{
	static inline void compute(mipp::reg regs_inter[N_FRAMES])
	{
	}
};

// HACK for clang
#ifdef __clang__
template <typename T, int N_FRAMES, int J>
struct Reorderer_static_core <T,N_FRAMES,J,16384>
{
	static inline void compute(mipp::reg regs_inter[N_FRAMES])
	{
	}
};
#endif

template <typename T, int N_FRAMES>
void Reorderer_static<T,N_FRAMES>
::apply(const std::vector<const T*> in_data, T* out_data, const int data_length)
{
	assert(in_data.size() == N_FRAMES);

	constexpr int n_fra = N_FRAMES;

	int start_seq_loop = 0;

	if (is_power_of_2((int)in_data.size()) && (int)in_data.size() >= 2 && (int)in_data.size() <= mipp::nElReg<T>())
	{
#ifndef _MSC_VER
		mipp::reg regs_inter[n_fra];
#else
		mipp::reg* regs_inter = new mipp::reg[n_fra];
#endif

		// vectorized reordering
		const auto loop_size = (int)(data_length/mipp::nElReg<T>());
		for (auto i = 0; i < loop_size; i++)
		{
			// loads
			for (auto f = 0; f < n_fra; f++)
				regs_inter[f] = mipp::loadu<T>(in_data[f] + i*mipp::nElReg<T>());

			// auto unrolled reordering
			Reorderer_static_core<T, n_fra, n_fra, 1>::compute(regs_inter);

#ifdef __AVX__
			// stores
			constexpr int n_fra_2 = n_fra / 2;
			for (auto f = 0; f < n_fra_2; f++)
				mipp::storeu<T>(out_data + (i*n_fra          +f) * mipp::nElReg<T>(), regs_inter[2*f +0]);
			for (auto f = 0; f < n_fra_2; f++)
				mipp::storeu<T>(out_data + (i*n_fra +n_fra_2 +f) * mipp::nElReg<T>(), regs_inter[2*f +1]);
#else
			// stores
			for (auto f = 0; f < n_fra; f++)
				mipp::storeu<T>(out_data + (i*n_fra +f) * mipp::nElReg<T>(), regs_inter[f]);
#endif
		}

		start_seq_loop = loop_size * mipp::nElReg<T>();

#ifdef _MSC_VER
		delete[] regs_inter;
#endif
	}

	// sequential reordering
	for (auto i = start_seq_loop; i < data_length; i++)
		for (auto f = 0; f < N_FRAMES; f++)
			out_data[i*N_FRAMES +f] = in_data[f][i];
}

template <typename T>
void Reorderer<T>
::apply_rev(const T* in_data, std::vector<T*> out_data, const int data_length)
{
	const int n_fra   = (int)out_data.size();
	const int n_fra_2 = n_fra / 2;

	int start_seq_loop = 0;

	if (is_power_of_2((int)out_data.size()) && (int)out_data.size() >= 2 && (int)out_data.size() <= mipp::nElReg<T>())
	{
		mipp::reg regs_inter[mipp::nElReg<T>()];

		// vectorized reordering
		const auto loop_size = (int)(data_length/mipp::nElReg<T>());
		for (auto i = 0; i < loop_size; i++)
		{
			// loads
			for (auto f = 0; f < n_fra; f++)
				regs_inter[f] = mipp::loadu<T>(in_data + (i*n_fra +f) * mipp::nElReg<T>());

			// reordering
			auto k_size = 1;
			auto l_size = n_fra_2;
			for (auto j = 1; j < mipp::nElReg<T>(); j <<= 1)
			{
				if (j > 1)
				{
					k_size <<= 1;
					l_size >>= 1;

					if (l_size == 0)
					{
						k_size = 1;
						l_size = n_fra_2;
					}
				}

				for (auto k = 0; k < k_size; k++)
				{
					const auto jump = k * l_size * 2;
					for (auto l = 0; l < l_size; l++)
					{
						const auto interx2 = mipp::interleave<T>(regs_inter[jump         +l], 
						                                         regs_inter[jump +l_size +l]);
						regs_inter[jump         +l] = interx2.val[0];
						regs_inter[jump +l_size +l] = interx2.val[1];
					}
				}
			}

			// stores
			auto f = 0;
			for (auto k = 0; k < k_size; k++)
			{
				const auto jump = k * l_size * 2;
				for (auto l = 0; l < l_size; l++)
				{
					mipp::storeu<T>(out_data[f++] + i*mipp::nElReg<T>(), regs_inter[jump         +l]);
					mipp::storeu<T>(out_data[f++] + i*mipp::nElReg<T>(), regs_inter[jump +l_size +l]);
				}
			}
		}

		start_seq_loop = loop_size * mipp::nElReg<T>();
	}

	// sequential reordering
	for (auto i = start_seq_loop; i < data_length; i++)
		for (auto f = 0; f < n_fra; f++)
			out_data[f][i] = in_data[i*n_fra +f];
}

template <typename T, int N_FRAMES, int J, int K, int L>
struct Reorderer_static_core_rev
{
	static inline int compute(mipp::reg regs_inter[N_FRAMES])
	{
		for (auto k = 0; k < K; k++)
		{
			for (auto l = 0; l < L; l++)
			{
// #ifdef __AVX__
// 				const auto interx2 = mipp::interleave2<T>(regs_inter[k*L*2 +l], regs_inter[k*L*2 +L +l]);
// #else
				const auto interx2 = mipp::interleave <T>(regs_inter[k*L*2 +l], regs_inter[k*L*2 +L +l]);
// #endif

				regs_inter[k*L*2    +l] = interx2.val[0];
				regs_inter[k*L*2 +L +l] = interx2.val[1];
			}
		}

		constexpr auto NEXT_J = J >> 1;
		constexpr auto NEXT_K = K << 1;
		constexpr auto NEXT_L = L >> 1;

		return Reorderer_static_core_rev<T,N_FRAMES,NEXT_J,NEXT_K,NEXT_L>::compute(regs_inter);
	}
};

template <typename T, int N_FRAMES, int J, int K>
struct Reorderer_static_core_rev <T,N_FRAMES,J,K,0>
{
	static inline int compute(mipp::reg regs_inter[N_FRAMES])
	{
		constexpr auto NEXT_J = J;
		constexpr auto NEXT_K = 1;
		constexpr auto NEXT_L = N_FRAMES >> 1;

		return Reorderer_static_core_rev<T,N_FRAMES,NEXT_J,NEXT_K,NEXT_L>::compute(regs_inter);
	}
};

// #ifdef __AVX__
// template <typename T, int N_FRAMES, int K, int L>
// struct Reorderer_static_core_rev <T,N_FRAMES,2,K,L>
// {
// 	static inline int compute(mipp::reg regs_inter[N_FRAMES])
// 	{
// 		mipp::reg regs_inter_tmp[N_FRAMES];

// 		for (auto k = 0; k < K; k++)
// 		{
// 			for (auto l = 0; l < L; l++)
// 			{
// 				const auto interx2 = mipp::interleave <T>(regs_inter[k*L*2 +l], regs_inter[k*L*2 +L +l]);

// 				regs_inter_tmp[k*L*2    +l] = interx2.val[0];
// 				regs_inter_tmp[k*L*2 +L +l] = interx2.val[1];
// 			}
// 		}

// 		for (auto f = 0; f < N_FRAMES/2; f++) regs_inter[            f] = regs_inter_tmp[2*f +0];
// 		for (auto f = 0; f < N_FRAMES/2; f++) regs_inter[N_FRAMES/2 +f] = regs_inter_tmp[2*f +1];

// 		return K;
// 	}
// };

// template <typename T, int N_FRAMES, int K>
// struct Reorderer_static_core_rev <T,N_FRAMES,2,K,0>
// {
// 	static inline int compute(mipp::reg regs_inter[N_FRAMES])
// 	{
// 		constexpr auto NEXT_J = 2;
// 		constexpr auto NEXT_K = 1;
// 		constexpr auto NEXT_L = N_FRAMES >> 1;

// 		return Reorderer_static_core_rev<T,N_FRAMES,NEXT_J,NEXT_K,NEXT_L>::compute(regs_inter);
// 	}
// };
// #endif

template <typename T, int N_FRAMES, int K, int L>
struct Reorderer_static_core_rev <T,N_FRAMES,1,K,L>
{
	static inline int compute(mipp::reg regs_inter[N_FRAMES])
	{
		return K >> 1;
	}
};

template <typename T, int N_FRAMES, int K>
struct Reorderer_static_core_rev <T,N_FRAMES,1,K,0>
{
	static inline int compute(mipp::reg regs_inter[N_FRAMES])
	{
		return K >> 1;
	}
};

template <typename T, int N_FRAMES>
void Reorderer_static<T,N_FRAMES>
::apply_rev(const T* in_data, std::vector<T*> out_data, const int data_length)
{
	assert(out_data.size() == N_FRAMES);

	constexpr int n_fra   = N_FRAMES;
	constexpr int n_fra_2 = N_FRAMES / 2;

	int start_seq_loop = 0;

	if (is_power_of_2((int)out_data.size()) && (int)out_data.size() >= 2 && (int)out_data.size() <= mipp::nElReg<T>())
	{
		mipp::reg regs_inter[mipp::nElReg<T>()];

		// vectorized reordering
		const auto loop_size = (int)(data_length/mipp::nElReg<T>());
		for (auto i = 0; i < loop_size; i++)
		{
			// loads
			for (auto f = 0; f < n_fra; f++)
				regs_inter[f] = mipp::loadu<T>(in_data + (i*n_fra +f) * mipp::nElReg<T>());

			// auto unrolled reordering
			const auto k_size = Reorderer_static_core_rev<T, n_fra, mipp::nElReg<T>(), 1, n_fra_2>::compute(regs_inter);
			const auto l_size = n_fra_2 / k_size;

			// stores
			auto f = 0;
			for (auto k = 0; k < k_size; k++)
			{
				const auto jump = k * l_size * 2;
				for (auto l = 0; l < l_size; l++)
				{
					mipp::storeu<T>(out_data[f++] + i*mipp::nElReg<T>(), regs_inter[jump         +l]);
					mipp::storeu<T>(out_data[f++] + i*mipp::nElReg<T>(), regs_inter[jump +l_size +l]);
				}
			}
		}

		start_seq_loop = loop_size * mipp::nElReg<T>();
	}

	// sequential reordering
	for (auto i = start_seq_loop; i < data_length; i++)
		for (auto f = 0; f < n_fra; f++)
			out_data[f][i] = in_data[i*n_fra +f];
}