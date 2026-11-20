// OpenBFME. GPL-3.0.
//
// FCurve (fork PARTHEAVENS-1, package B of docs/fork/MODULE_CENSUS.md): the function curve of the INI field type "FCurve", in RotWK 2.01 data only the
// Radius / Opacity / Angle of PartTheHeavensUpdate. Read from RotWK's game.dat (md5 daed3668, caveat S-001); the BFME2 decomp has the class as
// Rva002BF807 / Rva00504EADCurve (PartTheHeavensUpdate.cpp), not its parse:
//   * layout (0x2C bytes): InPadding + 0, OutPadding + 4 (HOLD 0, EXTRAPOLATE 1, CYCLE 2, CYCLE_OFFSET 3, MIRROR 4: RW 0xDB5B74), the keys + 8
//     (a vector of 16-byte keys: time, value, in tangent, out tangent, sorted by time).
//   * set RW 0x90ADF3 (time, value, in, out) -> RW 0x90ADB3: the lower bound by time; a key of the same time stays and no key is added.
//   * the field parse RW 0x73B723: the next token (RW 0x42DC9F); the keys cleared (RW 0x87209C) and both paddings HOLD; a token sscanf "%f" reads
//     (RW 0xBD05E8, format RW 0xBD423C) is one key (0, value, 0, 0); else strcmp (RW 0xBD06B4) with "FCurve" (RW 0xC25194) parses the block
//     (RW 0x90B120); else INIException(5, "'FCurve' expected").
//   * the block RW 0x90B120: a builder (RW 0x90A59B: the curve, pending, the key's time / value / tangents, hasIn, hasOut) and the field table
//     InPadding / OutPadding (parseIndexList RW 0x42E956 over RW 0xDB5B74, + 0 / + 4), Key (RW 0x90B030); then the builder's finish RW 0x90AF82.
//   * Key RW 0x90B030: T and V (getNextSubToken RW 0x42DD73 and scanReal RW 0x42EAAD), then at most two more tokens on the colon separators
//     (INI + 0x420): "I" or "O" (case-sensitive, each once) with its angle (RW 0x90A5F2: scanReal, more than 89.9 or less than -89.9 degrees is
//     INIException(3, ...), else tan(degrees * 0.0174532924), here the SimMath sin / cos quotient: S-7176); any other token is INIException(3, "'I' or 'O' expected, and only one of them").
//     The key goes to the builder (RW 0x90AE4F): the first key is held; each later key needs a later time (INIException(3, "Function curve time
//     must be increasing")), gives the held key its out tangent when it had none (the slope to this key, SSE single) and its in tangent from its out
//     tangent when it had none, sets the held key (RW 0x90ADF3) and holds this one with its in tangent (given, else the same slope).
//   * the finish RW 0x90AF82: nothing held is INIException(3, "Function curve does not have any keyframes"); a held key without its in tangent
//     takes its out tangent (or 0) when it is the only key, else the slope from the last set key; without its out tangent it takes the in tangent.
// The evaluation (operator() RW 0x90AAEF, the segment RW 0x90A9FA) is the client's: PartTheHeavensUpdate's decal only (S-7176), not ported.

#pragma once

#include <vector>

class INI;

struct FCurveKey
{
	float time = 0.0f;
	float value = 0.0f;
	float inTangent = 0.0f;
	float outTangent = 0.0f;
};

struct FCurve
{
	enum Padding
	{
		PAD_HOLD = 0,
		PAD_EXTRAPOLATE = 1,
		PAD_CYCLE = 2,
		PAD_CYCLE_OFFSET = 3,
		PAD_MIRROR = 4
	};

	int inPadding = PAD_HOLD;    ///< + 0
	int outPadding = PAD_HOLD;   ///< + 4
	std::vector<FCurveKey> keys; ///< + 8, sorted by time

	void set(float time, float value, float inTangent, float outTangent); // RW 0x90ADF3

	// RW 0x73B723 (the field parse of an FCurve member)
	static void parse(INI *ini, void *instance, void *store, const void *userData);
};
