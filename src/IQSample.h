//
//  IQSample.h
//  rtl
//
//  Created by Vincent Moscaritolo on 4/19/22.
//
#pragma once

#include <complex>
#include <vector>

typedef std::complex<float> IQSample;
typedef std::vector<IQSample> IQSampleVector;

typedef double Sample;
typedef std::vector<Sample> SampleVector;


/** Compute mean and RMS over a sample vector. */
inline void samples_mean_rms(const SampleVector& samples,
									  double& mean, double& rms)
{
	 Sample vsum = 0;
	 Sample vsumsq = 0;

	 size_t n = samples.size();
	 for (auto i = 0; i < n; i++) {
		  Sample v = samples[i];
		  vsum   += v;
		  vsumsq += v * v;
	 }

	 mean = vsum / n;
	 rms  = sqrt(vsumsq / n);
}
