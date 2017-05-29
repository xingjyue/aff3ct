#ifndef MODULATOR_CPM_HPP_
#define MODULATOR_CPM_HPP_

#include <cmath>
#include <fstream>
#include <string>

#include "Tools/Perf/MIPP/mipp.h"
#include "Tools/params.h"
#include "Tools/Math/max.h"

#include "../Modulator.hpp"

#include "CPM_parameters.hpp"
#include "CPE/Encoder_CPE_Rimoldi.hpp"
#include "BCJR/CPM_BCJR.hpp"

namespace aff3ct
{
namespace module
{
// TODO: warning: working for Rimoldi decomposition only!
template <typename B = int, typename R = float, typename Q = R, tools::proto_max<Q> MAX = tools::max_star>
class Modulator_CPM : public Modulator<B,R,Q>
{
	using SIN  = B;
	using SOUT = B;

protected:
	// inputs:
	const bool                    no_sig2;    // no computation of sigma^2

	// modulation data:
	CPM_parameters<SIN,SOUT>      cpm;        // all CPM parameters
	R                             cpm_h;      // modulation index = k/p
	R                             T_samp;     // sample duration  = 1/s_factor
	mipp::vector<R>               baseband;   // translation of base band vectors
	mipp::vector<R>               projection; // translation of filtering generator family
	const int                     n_sy;       // number of symbols for one frame after encoding without tail symbols
	const int                     n_sy_tl;    // number of symbols to send for one frame after encoding with tail symbols
	Encoder_CPE_Rimoldi<SIN,SOUT> cpe;        // the continuous phase encoder

	CPM_BCJR<SIN,SOUT,Q,MAX>      bcjr;       // demodulator

public:
	Modulator_CPM(int  N,
	              R    sigma,
	              int  bits_per_symbol,
	              int  sampling_factor,
	              int  cpm_L,
	              int  cpm_k,
	              int  cpm_p,
	              std::string mapping,
	              std::string wave_shape,
	              bool no_sig2,
	              int  n_frames = 1,
	              const std::string name = "Modulator_CPM");
	virtual ~Modulator_CPM();

	void set_sigma(const R sigma);

	static int size_mod(const int N, const int bps, const int L, const int ups)
	{
		return Modulator<B,R,Q>::get_buffer_size_after_modulation(N, bps, L, ups, true);
	}

	static int size_fil(const int N, const int bps, const int L, const int p)
	{
		int m_order   = (int)1 << bps;
		int n_wa      = (int)(p * std::pow(m_order, L));
		int n_bits_wa = (int)std::ceil(std::log2(n_wa));
		int max_wa_id = (int)(1 << n_bits_wa);

		return Modulator<B,R,Q>::get_buffer_size_after_filtering(N, bps, L, max_wa_id, false);
	}

protected:
	void   _modulate(const B *X_N1,                R *X_N2, const int frame_id);
	void     _filter(const R *Y_N1,                R *Y_N2, const int frame_id);
	void _demodulate(const Q *Y_N1,                Q *Y_N2, const int frame_id);
	void _demodulate(const Q *Y_N1, const Q *Y_N2, Q *Y_N3, const int frame_id);

private:
	void generate_baseband    (               );
	void generate_projection  (               );
	R calculate_phase_response(const R t_stamp);
};
}
}

#include "Modulator_CPM.hxx"

#endif /* MODULATOR_CPM_HPP_ */
