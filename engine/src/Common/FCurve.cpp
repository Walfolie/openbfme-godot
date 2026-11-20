// OpenBFME. GPL-3.0.
// See Common/FCurve.h (fork PARTHEAVENS-1: RotWK 2.01 RW 0x73B723, RW 0x90A59B .. 0x90B1A0).

#include "Common/FCurve.h"

#include "Common/INI.h"
#include "Common/INI/Msvcr71Real.h"
#include "Common/INIException.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace
{
// RW 0xDB5B74
const char *const kPaddingNames[] = { "HOLD", "EXTRAPOLATE", "CYCLE", "CYCLE_OFFSET", "MIRROR", nullptr };

// the slope between two keys: subss, subss, divss (RW 0x90AED8 .. 0x90AEEC)
float slope(float t0, float v0, float t1, float v1)
{
	return SimMath::divf32(SimMath::subf32(v1, v0), SimMath::subf32(t1, t0));
}

// RW 0x90A59B: the builder the Key rows feed (RW 0x90B120's local)
struct Builder
{
	FCurve *curve;
	bool pending = true; ///< + 4: no key held yet
	float time = 0.0f, value = 0.0f, inTangent = 0.0f, outTangent = 0.0f;
	bool hasIn = false, hasOut = false; ///< + 0x18 / + 0x19

	explicit Builder(FCurve *c)
		: curve(c)
	{
	}

	// RW 0x90AE4F
	void addKey(float t, float v, const float *in, const float *out)
	{
		if (pending)
		{
			time = t;
			value = v;
			pending = false;
			hasIn = in != nullptr;
			if (in)
			{
				inTangent = *in;
			}
			hasOut = out != nullptr;
			if (out)
			{
				outTangent = *out;
			}
			return;
		}
		if (time >= t)
		{
			throw INIException(3, "Function curve time must be increasing");
		}
		if (!hasOut)
		{
			outTangent = slope(time, value, t, v);
		}
		if (!hasIn)
		{
			inTangent = outTangent;
		}
		curve->set(time, value, inTangent, outTangent);
		inTangent = in ? *in : slope(time, value, t, v);
		hasIn = true;
		hasOut = out != nullptr;
		if (out)
		{
			outTangent = *out;
		}
		time = t;
		value = v;
	}

	// RW 0x90AF82
	void finish()
	{
		if (pending)
		{
			throw INIException(3, "Function curve does not have any keyframes");
		}
		if (curve->keys.empty())
		{
			if (!hasIn)
			{
				inTangent = hasOut ? outTangent : 0.0f;
			}
		}
		else if (!hasIn)
		{
			const FCurveKey &last = curve->keys.back();
			inTangent = slope(last.time, last.value, time, value);
		}
		if (!hasOut)
		{
			outTangent = inTangent;
		}
		curve->set(time, value, inTangent, outTangent);
	}
};

// RW 0x90A5F2: the tangent angle in degrees, as its slope
float scanTangent(INI *ini, const char *token)
{
	const float degrees = ini->scanReal(token);
	if (degrees > 89.9f) // RW 0xBDBD18
	{
		throw INIException(3, "Function curve tangent angle value must be less than 89.9 degrees");
	}
	if (-89.9f > degrees) // RW 0xC7ABE0
	{
		throw INIException(3, "Function curve tangent angle value must be greater than -89.9 degrees");
	}
	// fmul RW 0xBD1900 (PC24, an exact float), the CRT tan RW 0xA3D698 in double, fstp dword by the caller. INFERENCE (S-7176): tan as the
	// deterministic sin / cos quotient (one rounding more than the CRT's tan before the float store; RotWK's data has only 0 degrees)
	const float radians = SimMath::fstpDword(SimMath::pc24MulW((double)degrees, (double)0.0174532924f));
	return SimMath::fstpDword(SimMath::divD(SimMath::sind(radians), SimMath::cosd(radians)));
}

// RW 0x90B030
void parseKey(INI *ini, void *, void *, const void *userData)
{
	Builder &b = *(Builder *)userData;
	const float t = ini->scanReal(ini->getNextSubToken("T")); // RW 0xC1842C
	const float v = ini->scanReal(ini->getNextSubToken("V")); // RW 0xC7ACAC
	float in = 0.0f, out = 0.0f;
	const float *inPtr = nullptr, *outPtr = nullptr;
	for (int i = 0; i < 2; ++i)
	{
		const char *token = ini->getNextTokenOrNull(ini->getSepsColon()); // INI + 0x420
		if (!token)
		{
			break;
		}
		if (!inPtr && std::strcmp(token, "I") == 0)
		{
			in = scanTangent(ini, ini->getNextToken(ini->getSepsColon()));
			inPtr = &in;
		}
		else if (!outPtr && std::strcmp(token, "O") == 0)
		{
			out = scanTangent(ini, ini->getNextToken(ini->getSepsColon()));
			outPtr = &out;
		}
		else
		{
			throw INIException(3, "'I' or 'O' expected, and only one of them");
		}
	}
	b.addKey(t, v, inPtr, outPtr);
}
} // namespace

// RW 0x90ADF3 -> RW 0x90ADB3
void FCurve::set(float time, float value, float inTangent, float outTangent)
{
	auto it = std::lower_bound(keys.begin(), keys.end(), time, [](const FCurveKey &k, float t) { return k.time < t; });
	if (it != keys.end() && !(it->time > time))
	{
		return; // a key of this time stays
	}
	keys.insert(it, FCurveKey{ time, value, inTangent, outTangent });
}

// RW 0x73B723
void FCurve::parse(INI *ini, void *, void *store, const void *)
{
	FCurve &curve = *(FCurve *)store;
	const char *token = ini->getNextToken();
	curve.keys.clear(); // RW 0x87209C
	curve.inPadding = PAD_HOLD;
	curve.outPadding = PAD_HOLD;
	float constant = 0.0f;
	if (Msvcr71Real::scanfFloat(token, constant)) // sscanf "%f" (RW 0xBD05E8), one conversion
	{
		curve.set(0.0f, constant, 0.0f, 0.0f);
		return;
	}
	if (std::strcmp(token, "FCurve") != 0) // strcmp RW 0xBD06B4
	{
		throw INIException(5, "'FCurve' expected");
	}
	// RW 0x90B120
	Builder builder(&curve);
	const FieldParse table[] = {
		{ "InPadding", INI::parseIndexList, kPaddingNames, (int)offsetof(FCurve, inPadding) },
		{ "OutPadding", INI::parseIndexList, kPaddingNames, (int)offsetof(FCurve, outPadding) },
		{ "Key", parseKey, &builder, (int)offsetof(FCurve, keys) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(&curve, table);
	builder.finish();
}
