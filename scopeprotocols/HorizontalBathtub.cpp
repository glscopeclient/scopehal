/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include "../scopehal/scopehal.h"
#include "HorizontalBathtub.h"
#include "EyePattern.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

HorizontalBathtub::HorizontalBathtub(const string& color)
	: Filter(color, CAT_ANALYSIS)
{
	AddStream(Unit(Unit::UNIT_LOG_BER), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("din");

	m_voltageName = "Voltage";
	m_parameters[m_voltageName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_voltageName].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool HorizontalBathtub::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_EYE) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string HorizontalBathtub::GetProtocolName()
{
	return "Horz Bathtub";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void HorizontalBathtub::Refresh()
{
	if(!VerifyAllInputsOK(true))
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<EyeWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();
	float threshold = m_parameters[m_voltageName].GetFloatVal();

	//Find the eye bin for this height
	float yscale = din->GetHeight() / m_inputs[0].GetVoltageRange();
	float ymid = din->GetHeight()/2;
	float center = din->GetCenterVoltage();

	//Sanity check we're not off the eye
	size_t ybin = round( (threshold-center)*yscale + ymid);
	if(ybin >= din->GetHeight())
		return;

	//Horizontal scale: one eye is two UIs wide
	double fs_per_width = 2*din->m_uiWidth;
	double fs_per_pixel = fs_per_width / din->GetWidth();

	//Create the output
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();
	cap->m_triggerPhase = -din->m_uiWidth/2;
	cap->m_timescale = fs_per_pixel;

	//Extract the single scanline we're interested in
	//TODO: support non-NRZ waveforms
	size_t len = din->GetWidth();
	auto halflen = len/2;
	auto quartlen = halflen/2;
	cap->Resize(halflen);
	for(size_t i=0; i<halflen; i++)
	{
		auto ber = din->GetBERAtPoint(i + quartlen, ybin, din->GetWidth()/2, din->GetHeight()/2);
		if(ber < 1e-20)
			cap->m_samples[i] = -20;
		else
			cap->m_samples[i] = log10(ber);
	}

	//TODO: dual dirac extrapolation

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}
