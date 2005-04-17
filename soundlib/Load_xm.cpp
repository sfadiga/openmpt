/*
 * This program is  free software; you can redistribute it  and modify it
 * under the terms of the GNU  General Public License as published by the
 * Free Software Foundation; either version 2  of the license or (at your
 * option) any later version.
 *
 * Authors: Olivier Lapicque <olivierl@jps.net>
*/

#include "stdafx.h"
#include "sndfile.h"

////////////////////////////////////////////////////////
// FastTracker II XM file support

#pragma warning(disable:4244)

#pragma pack(1)
typedef struct tagXMFILEHEADER
{
	DWORD size;
	WORD norder;
	WORD restartpos;
	WORD channels;
	WORD patterns;
	WORD instruments;
	WORD flags;
	WORD speed;
	WORD tempo;
	BYTE order[256];
} XMFILEHEADER;


typedef struct tagXMINSTRUMENTHEADER
{
	DWORD size;
	CHAR name[22];
	BYTE type;
	BYTE samples;
	BYTE samplesh;
} XMINSTRUMENTHEADER;


typedef struct tagXMSAMPLEHEADER
{
	DWORD shsize;
	BYTE snum[96];
	WORD venv[24];
	WORD penv[24];
	BYTE vnum, pnum;
	BYTE vsustain, vloops, vloope, psustain, ploops, ploope;
	BYTE vtype, ptype;
	BYTE vibtype, vibsweep, vibdepth, vibrate;
	WORD volfade;
	WORD res;
	BYTE reserved1[20];
} XMSAMPLEHEADER;

typedef struct tagXMSAMPLESTRUCT
{
	DWORD samplen;
	DWORD loopstart;
	DWORD looplen;
	BYTE vol;
	signed char finetune;
	BYTE type;
	BYTE pan;
	signed char relnote;
	BYTE res;
	char name[22];
} XMSAMPLESTRUCT;
#pragma pack()


BOOL CSoundFile::ReadXM(const BYTE *lpStream, DWORD dwMemLength)
//--------------------------------------------------------------
{
	XMSAMPLEHEADER xmsh;
	XMSAMPLESTRUCT xmss;
	DWORD dwMemPos, dwHdrSize;
	WORD norders=0, restartpos=0, channels=0, patterns=0, instruments=0;
	WORD xmflags=0, deftempo=125, defspeed=6;
	BYTE InstUsed[256];
// -> CODE#0006
// -> DESC="misc quantity changes"
//	BYTE channels_used[MAX_CHANNELS];
	BYTE channels_used[MAX_BASECHANNELS];
// -! BEHAVIOUR_CHANGE#0006
	BYTE pattern_map[256];
	BYTE samples_used[(MAX_SAMPLES+7)/8];
	UINT unused_samples;

	m_nChannels = 0;
	if ((!lpStream) || (dwMemLength < 0x200)) return FALSE;
	if (strnicmp((LPCSTR)lpStream, "Extended Module", 15)) return FALSE;
	memcpy(m_szNames[0], lpStream+17, 20);
	dwHdrSize = *((DWORD *)(lpStream+60));
	norders = *((WORD *)(lpStream+64));
	if ((!norders) || (norders > MAX_ORDERS)) return FALSE;
	restartpos = *((WORD *)(lpStream+66));
	channels = *((WORD *)(lpStream+68));
// -> CODE#0006
// -> DESC="misc quantity changes"
//	if ((!channels) || (channels > 64)) return FALSE;
	if ((!channels) || (channels > MAX_BASECHANNELS)) return FALSE;
// -! BEHAVIOUR_CHANGE#0006
	m_nType = MOD_TYPE_XM;
	m_nMinPeriod = 27;
	m_nMaxPeriod = 54784;
	m_nChannels = channels;
	if (restartpos < norders) m_nRestartPos = restartpos;
	patterns = *((WORD *)(lpStream+70));
	if (patterns > 256) patterns = 256;
	instruments = *((WORD *)(lpStream+72));
	if (instruments >= MAX_INSTRUMENTS) instruments = MAX_INSTRUMENTS-1;
	m_nInstruments = instruments;
	m_nSamples = 0;
	memcpy(&xmflags, lpStream+74, 2);
	if (xmflags & 1) m_dwSongFlags |= SONG_LINEARSLIDES;
	if (xmflags & 0x1000) m_dwSongFlags |= SONG_EXFILTERRANGE;
	defspeed = *((WORD *)(lpStream+76));
	deftempo = *((WORD *)(lpStream+78));
// -> CODE#0016
// -> DESC="default tempo update"
//	if ((deftempo >= 32) && (deftempo < 256)) m_nDefaultTempo = deftempo;
	if ((deftempo >= 32) && (deftempo <= 512)) m_nDefaultTempo = deftempo;
	if ((defspeed > 0) && (defspeed < 40)) m_nDefaultSpeed = defspeed;
// -! BEHAVIOUR_CHANGE#0016
	memcpy(Order, lpStream+80, norders);
	memset(InstUsed, 0, sizeof(InstUsed));
	if (patterns > MAX_PATTERNS)
	{
		UINT i, j;
		for (i=0; i<norders; i++)
		{
			if (Order[i] < patterns) InstUsed[Order[i]] = TRUE;
		}
		j = 0;
		for (i=0; i<256; i++)
		{
			if (InstUsed[i]) pattern_map[i] = j++;
		}
		for (i=0; i<256; i++)
		{
			if (!InstUsed[i])
			{
				pattern_map[i] = (j < MAX_PATTERNS) ? j : 0xFE;
				j++;
			}
		}
		for (i=0; i<norders; i++)
		{
			Order[i] = pattern_map[Order[i]];
		}
	} else
	{
		for (UINT i=0; i<256; i++) pattern_map[i] = i;
	}
	memset(InstUsed, 0, sizeof(InstUsed));
	dwMemPos = dwHdrSize + 60;
	if (dwMemPos + 8 >= dwMemLength) return TRUE;
	// Reading patterns
	memset(channels_used, 0, sizeof(channels_used));
	for (UINT ipat=0; ipat<patterns; ipat++)
	{
		UINT ipatmap = pattern_map[ipat];
		DWORD dwSize = 0;
		WORD rows=64, packsize=0;
		dwSize = *((DWORD *)(lpStream+dwMemPos));
		while ((dwMemPos + dwSize >= dwMemLength) || (dwSize & 0xFFFFFF00))
		{
			if (dwMemPos + 4 >= dwMemLength) break;
			dwMemPos++;
			dwSize = *((DWORD *)(lpStream+dwMemPos));
		}
		rows = *((WORD *)(lpStream+dwMemPos+5));
// -> CODE#0008
// -> DESC="#define to set pattern size"
//		if ((!rows) || (rows > 256)) rows = 64;
		if ((!rows) || (rows > MAX_PATTERN_ROWS)) rows = 64;
// -> BEHAVIOUR_CHANGE#0008
		packsize = *((WORD *)(lpStream+dwMemPos+7));
		if (dwMemPos + dwSize + 4 > dwMemLength) return TRUE;
		dwMemPos += dwSize;
		if (dwMemPos + packsize + 4 > dwMemLength) return TRUE;
		MODCOMMAND *p;
		if (ipatmap < MAX_PATTERNS)
		{
			PatternSize[ipatmap] = rows;
			if ((Patterns[ipatmap] = AllocatePattern(rows, m_nChannels)) == NULL) return TRUE;
			if (!packsize) continue;
			p = Patterns[ipatmap];
		} else p = NULL;
		const BYTE *src = lpStream+dwMemPos;
		UINT j=0;
		for (UINT row=0; row<rows; row++)
		{
			for (UINT chn=0; chn<m_nChannels; chn++)
			{
				if ((p) && (j < packsize))
				{
					BYTE b = src[j++];
					UINT vol = 0;
					if (b & 0x80)
					{
						if (b & 1) p->note = src[j++];
						if (b & 2) p->instr = src[j++];
						if (b & 4) vol = src[j++];
						if (b & 8) p->command = src[j++];
						if (b & 16) p->param = src[j++];
					} else
					{
						p->note = b;
						p->instr = src[j++];
						vol = src[j++];
						p->command = src[j++];
						p->param = src[j++];
					}
					if (p->note == 97) p->note = 0xFF; else
					if ((p->note) && (p->note < 97)) p->note += 12;
					if (p->note) channels_used[chn] = 1;
					if (p->command | p->param) ConvertModCommand(p);
					if (p->instr == 0xff) p->instr = 0;
					if (p->instr) InstUsed[p->instr] = TRUE;
					if ((vol >= 0x10) && (vol <= 0x50))
					{
						p->volcmd = VOLCMD_VOLUME;
						p->vol = vol - 0x10;
					} else
					if (vol >= 0x60)
					{
						UINT v = vol & 0xF0;
						vol &= 0x0F;
						p->vol = vol;
						switch(v)
						{
						// 60-6F: Volume Slide Down
						case 0x60:	p->volcmd = VOLCMD_VOLSLIDEDOWN; break;
						// 70-7F: Volume Slide Up:
						case 0x70:	p->volcmd = VOLCMD_VOLSLIDEUP; break;
						// 80-8F: Fine Volume Slide Down
						case 0x80:	p->volcmd = VOLCMD_FINEVOLDOWN; break;
						// 90-9F: Fine Volume Slide Up
						case 0x90:	p->volcmd = VOLCMD_FINEVOLUP; break;
						// A0-AF: Set Vibrato Speed
						case 0xA0:	p->volcmd = VOLCMD_VIBRATOSPEED; break;
						// B0-BF: Vibrato
						case 0xB0:	p->volcmd = VOLCMD_VIBRATO; break;
						// C0-CF: Set Panning
						case 0xC0:	p->volcmd = VOLCMD_PANNING; p->vol = (vol << 2) + 2; break;
						// D0-DF: Panning Slide Left
						case 0xD0:	p->volcmd = VOLCMD_PANSLIDELEFT; break;
						// E0-EF: Panning Slide Right
						case 0xE0:	p->volcmd = VOLCMD_PANSLIDERIGHT; break;
						// F0-FF: Tone Portamento
						case 0xF0:	p->volcmd = VOLCMD_TONEPORTAMENTO; break;
						}
					}
					p++;
				} else
				if (j < packsize)
				{
					BYTE b = src[j++];
					if (b & 0x80)
					{
						if (b & 1) j++;
						if (b & 2) j++;
						if (b & 4) j++;
						if (b & 8) j++;
						if (b & 16) j++;
					} else j += 4;
				} else break;
			}
		}
		dwMemPos += packsize;
	}
	// Wrong offset check
	while (dwMemPos + 4 < dwMemLength)
	{
		DWORD d = *((DWORD *)(lpStream+dwMemPos));
		if (d < 0x300) break;
		dwMemPos++;
	}
	memset(samples_used, 0, sizeof(samples_used));
	unused_samples = 0;
	// Reading instruments
	for (UINT iIns=1; iIns<=instruments; iIns++)
	{
		XMINSTRUMENTHEADER *pih;
		BYTE flags[32];
		DWORD samplesize[32];
		UINT samplemap[32];
		WORD nsamples;
				
		if (dwMemPos + sizeof(XMINSTRUMENTHEADER) >= dwMemLength) return TRUE;
		pih = (XMINSTRUMENTHEADER *)(lpStream+dwMemPos);
		if (dwMemPos + pih->size > dwMemLength) return TRUE;
		if ((Headers[iIns] = new INSTRUMENTHEADER) == NULL) continue;
		memset(Headers[iIns], 0, sizeof(INSTRUMENTHEADER));
		memcpy(Headers[iIns]->name, pih->name, 22);
		if ((nsamples = pih->samples) > 0)
		{
			if (dwMemPos + sizeof(XMSAMPLEHEADER) > dwMemLength) return TRUE;
			memcpy(&xmsh, lpStream+dwMemPos+sizeof(XMINSTRUMENTHEADER), sizeof(XMSAMPLEHEADER));
			dwMemPos += pih->size;
		} else
		{
			if (pih->size) dwMemPos += pih->size;
			else dwMemPos += sizeof(XMINSTRUMENTHEADER);
			continue;
		}
		memset(samplemap, 0, sizeof(samplemap));
		if (nsamples > 32) return TRUE;
		UINT newsamples = m_nSamples;
		for (UINT nmap=0; nmap<nsamples; nmap++)
		{
			UINT n = m_nSamples+nmap+1;
			if (n >= MAX_SAMPLES)
			{
				n = m_nSamples;
				while (n > 0)
				{
					if (!Ins[n].pSample)
					{
						for (UINT xmapchk=0; xmapchk < nmap; xmapchk++)
						{
							if (samplemap[xmapchk] == n) goto alreadymapped;
						}
						for (UINT clrs=1; clrs<iIns; clrs++) if (Headers[clrs])
						{
							INSTRUMENTHEADER *pks = Headers[clrs];
							for (UINT ks=0; ks<128; ks++)
							{
								if (pks->Keyboard[ks] == n) pks->Keyboard[ks] = 0;
							}
						}
						break;
					}
				alreadymapped:
					n--;
				}
#ifndef FASTSOUNDLIB
				// Damn! more than 200 samples: look for duplicates
				if (!n)
				{
					if (!unused_samples)
					{
						unused_samples = DetectUnusedSamples(samples_used);
						if (!unused_samples) unused_samples = 0xFFFF;
					}
					if ((unused_samples) && (unused_samples != 0xFFFF))
					{
						for (UINT iext=m_nSamples; iext>=1; iext--) if (0 == (samples_used[iext>>3] & (1<<(iext&7))))
						{
							unused_samples--;
							samples_used[iext>>3] |= (1<<(iext&7));
							DestroySample(iext);
							n = iext;
							for (UINT mapchk=0; mapchk<nmap; mapchk++)
							{
								if (samplemap[mapchk] == n) samplemap[mapchk] = 0;
							}
							for (UINT clrs=1; clrs<iIns; clrs++) if (Headers[clrs])
							{
								INSTRUMENTHEADER *pks = Headers[clrs];
								for (UINT ks=0; ks<128; ks++)
								{
									if (pks->Keyboard[ks] == n) pks->Keyboard[ks] = 0;
								}
							}
							memset(&Ins[n], 0, sizeof(Ins[0]));
							break;
						}
					}
				}
#endif // FASTSOUNDLIB
			}
			if (newsamples < n) newsamples = n;
			samplemap[nmap] = n;
		}
		m_nSamples = newsamples;
		// Reading Volume Envelope
		INSTRUMENTHEADER *penv = Headers[iIns];
		penv->nMidiProgram = pih->type;
		penv->nFadeOut = xmsh.volfade;
		penv->nPan = 128;
		penv->nPPC = 5*12;
		if (xmsh.vtype & 1) penv->dwFlags |= ENV_VOLUME;
		if (xmsh.vtype & 2) penv->dwFlags |= ENV_VOLSUSTAIN;
		if (xmsh.vtype & 4) penv->dwFlags |= ENV_VOLLOOP;
		if (xmsh.ptype & 1) penv->dwFlags |= ENV_PANNING;
		if (xmsh.ptype & 2) penv->dwFlags |= ENV_PANSUSTAIN;
		if (xmsh.ptype & 4) penv->dwFlags |= ENV_PANLOOP;
		if (xmsh.vnum > 12) xmsh.vnum = 12;
		if (xmsh.pnum > 12) xmsh.pnum = 12;
		penv->nVolEnv = xmsh.vnum;
		if (!xmsh.vnum) penv->dwFlags &= ~ENV_VOLUME;
		if (!xmsh.pnum) penv->dwFlags &= ~ENV_PANNING;
		penv->nPanEnv = xmsh.pnum;
		penv->nVolSustainBegin = penv->nVolSustainEnd = xmsh.vsustain;
		if (xmsh.vsustain >= 12) penv->dwFlags &= ~ENV_VOLSUSTAIN;
		penv->nVolLoopStart = xmsh.vloops;
		penv->nVolLoopEnd = xmsh.vloope;
		if (penv->nVolLoopEnd >= 12) penv->nVolLoopEnd = 0;
		if (penv->nVolLoopStart >= penv->nVolLoopEnd) penv->dwFlags &= ~ENV_VOLLOOP;
		penv->nPanSustainBegin = penv->nPanSustainEnd = xmsh.psustain;
		if (xmsh.psustain >= 12) penv->dwFlags &= ~ENV_PANSUSTAIN;
		penv->nPanLoopStart = xmsh.ploops;
		penv->nPanLoopEnd = xmsh.ploope;
		if (penv->nPanLoopEnd >= 12) penv->nPanLoopEnd = 0;
		if (penv->nPanLoopStart >= penv->nPanLoopEnd) penv->dwFlags &= ~ENV_PANLOOP;
		penv->nGlobalVol = 64;
		for (UINT ienv=0; ienv<12; ienv++)
		{
			penv->VolPoints[ienv] = (WORD)xmsh.venv[ienv*2];
			penv->VolEnv[ienv] = (BYTE)xmsh.venv[ienv*2+1];
			penv->PanPoints[ienv] = (WORD)xmsh.penv[ienv*2];
			penv->PanEnv[ienv] = (BYTE)xmsh.penv[ienv*2+1];
			if (ienv)
			{
				if (penv->VolPoints[ienv] < penv->VolPoints[ienv-1])
				{
					penv->VolPoints[ienv] &= 0xFF;
					penv->VolPoints[ienv] += penv->VolPoints[ienv-1] & 0xFF00;
					if (penv->VolPoints[ienv] < penv->VolPoints[ienv-1]) penv->VolPoints[ienv] += 0x100;
				}
				if (penv->PanPoints[ienv] < penv->PanPoints[ienv-1])
				{
					penv->PanPoints[ienv] &= 0xFF;
					penv->PanPoints[ienv] += penv->PanPoints[ienv-1] & 0xFF00;
					if (penv->PanPoints[ienv] < penv->PanPoints[ienv-1]) penv->PanPoints[ienv] += 0x100;
				}
			}
		}
		for (UINT j=0; j<96; j++)
		{
			penv->NoteMap[j+12] = j+1+12;
			if (xmsh.snum[j] < nsamples)
				penv->Keyboard[j+12] = samplemap[xmsh.snum[j]];
		}
		// Reading samples
		for (UINT ins=0; ins<nsamples; ins++)
		{
			if ((dwMemPos + sizeof(xmss) > dwMemLength)
			 || (dwMemPos + xmsh.shsize > dwMemLength)) return TRUE;
			memcpy(&xmss, lpStream+dwMemPos, sizeof(xmss));
			dwMemPos += xmsh.shsize;
			flags[ins] = (xmss.type & 0x10) ? RS_PCM16D : RS_PCM8D;
			if (xmss.type & 0x20) flags[ins] = (xmss.type & 0x10) ? RS_STPCM16D : RS_STPCM8D;
			samplesize[ins] = xmss.samplen;
			if (!samplemap[ins]) continue;
			if (xmss.type & 0x10)
			{
				xmss.looplen >>= 1;
				xmss.loopstart >>= 1;
				xmss.samplen >>= 1;
			}
			if (xmss.type & 0x20)
			{
				xmss.looplen >>= 1;
				xmss.loopstart >>= 1;
				xmss.samplen >>= 1;
			}
			if (xmss.samplen > MAX_SAMPLE_LENGTH) xmss.samplen = MAX_SAMPLE_LENGTH;
			if (xmss.loopstart >= xmss.samplen) xmss.type &= ~3;
			xmss.looplen += xmss.loopstart;
			if (xmss.looplen > xmss.samplen) xmss.looplen = xmss.samplen;
			if (!xmss.looplen) xmss.type &= ~3;
			UINT imapsmp = samplemap[ins];
			memcpy(m_szNames[imapsmp], xmss.name, 22);
			m_szNames[imapsmp][22] = 0;
			MODINSTRUMENT *pins = &Ins[imapsmp];
			pins->nLength = (xmss.samplen > MAX_SAMPLE_LENGTH) ? MAX_SAMPLE_LENGTH : xmss.samplen;
			pins->nLoopStart = xmss.loopstart;
			pins->nLoopEnd = xmss.looplen;
			if (pins->nLoopEnd > pins->nLength) pins->nLoopEnd = pins->nLength;
			if (pins->nLoopStart >= pins->nLoopEnd)
			{
				pins->nLoopStart = pins->nLoopEnd = 0;
			}
			if (xmss.type & 3) pins->uFlags |= CHN_LOOP;
			if (xmss.type & 2) pins->uFlags |= CHN_PINGPONGLOOP;
			pins->nVolume = xmss.vol << 2;
			if (pins->nVolume > 256) pins->nVolume = 256;
			pins->nGlobalVol = 64;
			if ((xmss.res == 0xAD) && (!(xmss.type & 0x30)))
			{
				flags[ins] = RS_ADPCM4;
				samplesize[ins] = (samplesize[ins]+1)/2 + 16;
			}
			pins->nFineTune = xmss.finetune;
			pins->RelativeTone = (int)xmss.relnote;
			pins->nPan = xmss.pan;
			pins->uFlags |= CHN_PANNING;
			pins->nVibType = xmsh.vibtype;
			pins->nVibSweep = xmsh.vibsweep;
			pins->nVibDepth = xmsh.vibdepth;
			pins->nVibRate = xmsh.vibrate;
			memcpy(pins->name, xmss.name, 22);
			pins->name[21] = 0;
		}
#if 0
		if ((xmsh.reserved2 > nsamples) && (xmsh.reserved2 <= 16))
		{
			dwMemPos += (((UINT)xmsh.reserved2) - nsamples) * xmsh.shsize;
		}
#endif
		for (UINT ismpd=0; ismpd<nsamples; ismpd++)
		{
			if ((samplemap[ismpd]) && (samplesize[ismpd]) && (dwMemPos < dwMemLength))
			{
				ReadSample(&Ins[samplemap[ismpd]], flags[ismpd], (LPSTR)(lpStream + dwMemPos), dwMemLength - dwMemPos);
			}
			dwMemPos += samplesize[ismpd];
			if (dwMemPos >= dwMemLength) break;
		}
	}
	// Read song comments: "TEXT"
	if ((dwMemPos + 8 < dwMemLength) && (*((DWORD *)(lpStream+dwMemPos)) == 0x74786574))
	{
		UINT len = *((DWORD *)(lpStream+dwMemPos+4));
		dwMemPos += 8;
		if ((dwMemPos + len <= dwMemLength) && (len < 16384))
		{
			m_lpszSongComments = new char[len+1];
			if (m_lpszSongComments)
			{
				memcpy(m_lpszSongComments, lpStream+dwMemPos, len);
				m_lpszSongComments[len] = 0;
			}
			dwMemPos += len;
		}
	}
	// Read midi config: "MIDI"
	if ((dwMemPos + 8 < dwMemLength) && (*((DWORD *)(lpStream+dwMemPos)) == 0x4944494D))
	{
		UINT len = *((DWORD *)(lpStream+dwMemPos+4));
		dwMemPos += 8;
		if (len == sizeof(MODMIDICFG))
		{
			memcpy(&m_MidiCfg, lpStream+dwMemPos, len);
			m_dwSongFlags |= SONG_EMBEDMIDICFG;
			dwMemPos += len;	//rewbs.fix36946
		}
	}
	// Read pattern names: "PNAM"
	if ((dwMemPos + 8 < dwMemLength) && (*((DWORD *)(lpStream+dwMemPos)) == 0x4d414e50))
	{
		UINT len = *((DWORD *)(lpStream+dwMemPos+4));
		dwMemPos += 8;
		if ((dwMemPos + len <= dwMemLength) && (len <= MAX_PATTERNS*MAX_PATTERNNAME) && (len >= MAX_PATTERNNAME))
		{
			m_lpszPatternNames = new char[len];
			if (m_lpszPatternNames)
			{
				m_nPatternNames = len / MAX_PATTERNNAME;
				memcpy(m_lpszPatternNames, lpStream+dwMemPos, len);
			}
			dwMemPos += len;
		}
	}
	// Read channel names: "CNAM"
	if ((dwMemPos + 8 < dwMemLength) && (*((DWORD *)(lpStream+dwMemPos)) == 0x4d414e43))
	{
		UINT len = *((DWORD *)(lpStream+dwMemPos+4));
		dwMemPos += 8;
		if ((dwMemPos + len <= dwMemLength) && (len <= MAX_BASECHANNELS*MAX_CHANNELNAME))
		{
			UINT n = len / MAX_CHANNELNAME;
			for (UINT i=0; i<n; i++)
			{
				memcpy(ChnSettings[i].szName, (lpStream+dwMemPos+i*MAX_CHANNELNAME), MAX_CHANNELNAME);
				ChnSettings[i].szName[MAX_CHANNELNAME-1] = 0;
			}
			dwMemPos += len;
		}
	}
	// Read mix plugins information
	if (dwMemPos + 8 < dwMemLength) 
	{
		dwMemPos += LoadMixPlugins(lpStream+dwMemPos, dwMemLength-dwMemPos);
	}

// -> CODE#0027
// -> DESC="per-instrument volume ramping setup (refered as attack)"

	// Leave if no extra instrument settings are available (end of file reached)
	if(dwMemPos >= dwMemLength) return TRUE;

	// Get file pointer to match the first byte of extra settings informations
	BYTE * ptr = (BYTE *)(lpStream + dwMemPos);

	// Seek for supported extended settings header
	__int16 size = 0;
	__int32 code = (*((__int32 *)ptr));

	// Instrument extensions
	if( code == 'MPTX' && m_nInstruments ){
		ptr += sizeof(__int32);							// jump extension header code
		while( (DWORD)(ptr - lpStream) < dwMemLength ){ //Loop 'till end of file looking for inst. extensions

			code = (*((__int32 *)ptr));			// read field code
			if (code == 'MPTS') {				//Reached song extensions, break out of this loop
				break;
			}
			
			ptr += sizeof(__int32);				// jump field code
			size = (*((__int16 *)ptr));			// read field size
			ptr += sizeof(__int16);				// jump field size

			for(UINT nins=1; nins<=m_nInstruments; nins++){
				if(Headers[nins]){
					// get field's adress in instrument's header
					BYTE * fadr = GetInstrumentHeaderFieldPointer(Headers[nins], code, size);
					// copy field data in instrument's header (except for keyboard mapping)
					if(fadr && code != 'K[..') memcpy(fadr,ptr,size);
					// jump field
					ptr += size;
				}
			}
			//end rewbs.instroVSTi
		}
	}
// -! NEW_FEATURE#0027

	// Song extensions
	if( code == 'MPTS' ){
		ptr += sizeof(__int32); // jump extension header code
		while( (DWORD)(ptr - lpStream) < dwMemLength ){ //Loop 'till end of file looking for song extensions
			code = (*((__int32 *)ptr));			// read field code
			ptr += sizeof(__int32);				// jump field code
			size = (*((__int16 *)ptr));			// read field size
			ptr += sizeof(__int16);				// jump field size

			BYTE * fadr = NULL;
			switch (code) {						// interpret field code
				case 'DT..': fadr = reinterpret_cast<BYTE*>(&m_nDefaultTempo);   break;
				case 'RPB.': fadr = reinterpret_cast<BYTE*>(&m_nRowsPerBeat);    break;
				case 'RPM.': fadr = reinterpret_cast<BYTE*>(&m_nRowsPerMeasure); break;
			}

			if (fadr != NULL) {					// if field code recognized
				memcpy(fadr,ptr,size);			// read field data
			}
			ptr += size;						// jump field data
		}
	}

	return TRUE;
}


#ifndef MODPLUG_NO_FILESAVE

BOOL CSoundFile::SaveXM(LPCSTR lpszFileName, UINT nPacking)
//---------------------------------------------------------
{
	BYTE s[64*64*5];
	XMFILEHEADER header;
	XMINSTRUMENTHEADER xmih;
	XMSAMPLEHEADER xmsh;
	XMSAMPLESTRUCT xmss;
	WORD smptable[32];
	BYTE xmph[9];
	FILE *f;
	int i;

	if ((!m_nChannels) || (!lpszFileName)) return FALSE;
	if ((f = fopen(lpszFileName, "wb")) == NULL) return FALSE;
	fwrite("Extended Module: ", 17, 1, f);
	fwrite(m_szNames[0], 20, 1, f);
	s[0] = 0x1A;
	lstrcpy((LPSTR)&s[1], (nPacking) ? "MOD Plugin packed   " : "FastTracker v2.00   ");
	s[21] = 0x04;
	s[22] = 0x01;
	fwrite(s, 23, 1, f);
	// Writing song header
	memset(&header, 0, sizeof(header));
	header.size = sizeof(XMFILEHEADER);
	header.norder = 0;
	header.restartpos = m_nRestartPos;
	header.channels = m_nChannels;
	header.patterns = 0;
	for (i=0; i<MAX_ORDERS; i++)
	{
// -> CODE#0013
// -> DESC="load/save the whole pattern order list"
//		if (Order[i] == 0xFF) break;
// -! CODE#0013
		header.norder++;
		if ((Order[i] >= header.patterns) && (Order[i] < MAX_PATTERNS)) header.patterns = Order[i]+1;
	}
	header.instruments = m_nInstruments;
	if (!header.instruments) header.instruments = m_nSamples;
	header.flags = (m_dwSongFlags & SONG_LINEARSLIDES) ? 0x01 : 0x00;
	if (m_dwSongFlags & SONG_EXFILTERRANGE) header.flags |= 0x1000;
	header.tempo = m_nDefaultTempo;
	header.speed = m_nDefaultSpeed;
	memcpy(header.order, Order, header.norder);
	fwrite(&header, 1, sizeof(header), f);
	// Writing patterns
	for (i=0; i<header.patterns; i++) if (Patterns[i])
	{
		MODCOMMAND *p = Patterns[i];
		UINT len = 0;

		memset(&xmph, 0, sizeof(xmph));
		xmph[0] = 9;
		xmph[5] = (BYTE)(PatternSize[i] & 0xFF);
		xmph[6] = (BYTE)(PatternSize[i] >> 8);
		for (UINT j=m_nChannels*PatternSize[i]; j; j--,p++)
		{
			UINT note = p->note;
			UINT param = ModSaveCommand(p, TRUE);
			UINT command = param >> 8;
			param &= 0xFF;
			if (note >= 0xFE) note = 97; else
			if ((note <= 12) || (note > 96+12)) note = 0; else
			note -= 12;
			UINT vol = 0;
			if (p->volcmd)
			{
				UINT volcmd = p->volcmd;
				switch(volcmd)
				{
				case VOLCMD_VOLUME:			vol = 0x10 + p->vol; break;
				case VOLCMD_VOLSLIDEDOWN:	vol = 0x60 + (p->vol & 0x0F); break;
				case VOLCMD_VOLSLIDEUP:		vol = 0x70 + (p->vol & 0x0F); break;
				case VOLCMD_FINEVOLDOWN:	vol = 0x80 + (p->vol & 0x0F); break;
				case VOLCMD_FINEVOLUP:		vol = 0x90 + (p->vol & 0x0F); break;
				case VOLCMD_VIBRATOSPEED:	vol = 0xA0 + (p->vol & 0x0F); break;
				case VOLCMD_VIBRATO:		vol = 0xB0 + (p->vol & 0x0F); break;
				case VOLCMD_PANNING:		vol = 0xC0 + (p->vol >> 2); if (vol > 0xCF) vol = 0xCF; break;
				case VOLCMD_PANSLIDELEFT:	vol = 0xD0 + (p->vol & 0x0F); break;
				case VOLCMD_PANSLIDERIGHT:	vol = 0xE0 + (p->vol & 0x0F); break;
				case VOLCMD_TONEPORTAMENTO:	vol = 0xF0 + (p->vol & 0x0F); break;
				}
			}
			if ((note) && (p->instr) && (vol > 0x0F) && (command) && (param))
			{
				s[len++] = note;
				s[len++] = p->instr;
				s[len++] = vol;
				s[len++] = command;
				s[len++] = param;
			} else
			{
				BYTE b = 0x80;
				if (note) b |= 0x01;
				if (p->instr) b |= 0x02;
				if (vol >= 0x10) b |= 0x04;
				if (command) b |= 0x08;
				if (param) b |= 0x10;
				s[len++] = b;
				if (b & 1) s[len++] = note;
				if (b & 2) s[len++] = p->instr;
				if (b & 4) s[len++] = vol;
				if (b & 8) s[len++] = command;
				if (b & 16) s[len++] = param;
			}
			if (len > sizeof(s) - 5) break;
		}
		xmph[7] = (BYTE)(len & 0xFF);
		xmph[8] = (BYTE)(len >> 8);
		fwrite(xmph, 1, 9, f);
		fwrite(s, 1, len, f);
	} else
	{
		memset(&xmph, 0, sizeof(xmph));
		xmph[0] = 9;
		xmph[5] = (BYTE)(PatternSize[i] & 0xFF);
		xmph[6] = (BYTE)(PatternSize[i] >> 8);
		fwrite(xmph, 1, 9, f);
	}
	// Writing instruments
	for (i=1; i<=header.instruments; i++)
	{
		MODINSTRUMENT *pins;
		BYTE flags[32];

		memset(&xmih, 0, sizeof(xmih));
		memset(&xmsh, 0, sizeof(xmsh));
		xmih.size = sizeof(xmih) + sizeof(xmsh);
		memcpy(xmih.name, m_szNames[i], 22);
		xmih.type = 0;
		xmih.samples = 0;
		if (m_nInstruments)
		{
			INSTRUMENTHEADER *penv = Headers[i];
			if (penv)
			{
				memcpy(xmih.name, penv->name, 22);
				xmih.type = penv->nMidiProgram;
				xmsh.volfade = penv->nFadeOut;
				xmsh.vnum = (BYTE)penv->nVolEnv;
				xmsh.pnum = (BYTE)penv->nPanEnv;
				if (xmsh.vnum > 12) xmsh.vnum = 12;
				if (xmsh.pnum > 12) xmsh.pnum = 12;
				for (UINT ienv=0; ienv<12; ienv++)
				{
					xmsh.venv[ienv*2] = penv->VolPoints[ienv];
					xmsh.venv[ienv*2+1] = penv->VolEnv[ienv];
					xmsh.penv[ienv*2] = penv->PanPoints[ienv];
					xmsh.penv[ienv*2+1] = penv->PanEnv[ienv];
				}
				if (penv->dwFlags & ENV_VOLUME) xmsh.vtype |= 1;
				if (penv->dwFlags & ENV_VOLSUSTAIN) xmsh.vtype |= 2;
				if (penv->dwFlags & ENV_VOLLOOP) xmsh.vtype |= 4;
				if (penv->dwFlags & ENV_PANNING) xmsh.ptype |= 1;
				if (penv->dwFlags & ENV_PANSUSTAIN) xmsh.ptype |= 2;
				if (penv->dwFlags & ENV_PANLOOP) xmsh.ptype |= 4;
				xmsh.vsustain = (BYTE)penv->nVolSustainBegin;
				xmsh.vloops = (BYTE)penv->nVolLoopStart;
				xmsh.vloope = (BYTE)penv->nVolLoopEnd;
				xmsh.psustain = (BYTE)penv->nPanSustainBegin;
				xmsh.ploops = (BYTE)penv->nPanLoopStart;
				xmsh.ploope = (BYTE)penv->nPanLoopEnd;
				for (UINT j=0; j<96; j++) if (penv->Keyboard[j+12])
				{
					UINT k;
					for (k=0; k<xmih.samples; k++)	if (smptable[k] == penv->Keyboard[j+12]) break;
					if (k == xmih.samples)
					{
						smptable[xmih.samples++] = penv->Keyboard[j+12];
					}
					if (xmih.samples >= 32) break;
					xmsh.snum[j] = k;
				}
//				xmsh.reserved2 = xmih.samples;
			}
		} else
		{
			xmih.samples = 1;
//			xmsh.reserved2 = 1;
			smptable[0] = i;
		}
		xmsh.shsize = (xmih.samples) ? 40 : 0;
		fwrite(&xmih, 1, sizeof(xmih), f);
		if (smptable[0])
		{
			MODINSTRUMENT *pvib = &Ins[smptable[0]];
			xmsh.vibtype = pvib->nVibType;
			xmsh.vibsweep = pvib->nVibSweep;
			xmsh.vibdepth = pvib->nVibDepth;
			xmsh.vibrate = pvib->nVibRate;
		}
		fwrite(&xmsh, 1, xmih.size - sizeof(xmih), f);
		if (!xmih.samples) continue;
		for (UINT ins=0; ins<xmih.samples; ins++)
		{
			memset(&xmss, 0, sizeof(xmss));
			if (smptable[ins]) memcpy(xmss.name, m_szNames[smptable[ins]], 22);
			pins = &Ins[smptable[ins]];
			xmss.samplen = pins->nLength;
			xmss.loopstart = pins->nLoopStart;
			xmss.looplen = pins->nLoopEnd - pins->nLoopStart;
			xmss.vol = pins->nVolume / 4;
			xmss.finetune = (char)pins->nFineTune;
			xmss.type = 0;
			if (pins->uFlags & CHN_LOOP) xmss.type = (pins->uFlags & CHN_PINGPONGLOOP) ? 2 : 1;
			flags[ins] = RS_PCM8D;
#ifndef NO_PACKING
			if (nPacking)
			{
				if ((!(pins->uFlags & (CHN_16BIT|CHN_STEREO)))
				 && (CanPackSample(pins->pSample, pins->nLength, nPacking)))
				{
					flags[ins] = RS_ADPCM4;
					xmss.res = 0xAD;
				}
			} else
#endif
			{
				if (pins->uFlags & CHN_16BIT)
				{
					flags[ins] = RS_PCM16D;
					xmss.type |= 0x10;
					xmss.looplen *= 2;
					xmss.loopstart *= 2;
					xmss.samplen *= 2;
				}
				if (pins->uFlags & CHN_STEREO)
				{
					flags[ins] = (pins->uFlags & CHN_16BIT) ? RS_STPCM16D : RS_STPCM8D;
					xmss.type |= 0x20;
					xmss.looplen *= 2;
					xmss.loopstart *= 2;
					xmss.samplen *= 2;
				}
			}
			xmss.pan = 255;
			if (pins->nPan < 256) xmss.pan = (BYTE)pins->nPan;
			xmss.relnote = (signed char)pins->RelativeTone;
			fwrite(&xmss, 1, xmsh.shsize, f);
		}
		for (UINT ismpd=0; ismpd<xmih.samples; ismpd++)
		{
			pins = &Ins[smptable[ismpd]];
			if (pins->pSample)
			{
#ifndef NO_PACKING
				if ((flags[ismpd] == RS_ADPCM4) && (xmih.samples>1)) CanPackSample(pins->pSample, pins->nLength, nPacking);
#endif // NO_PACKING
				WriteSample(f, pins, flags[ismpd]);
			}
		}
	}
	// Writing song comments
	if ((m_lpszSongComments) && (m_lpszSongComments[0]))
	{
		DWORD d = 0x74786574;
		fwrite(&d, 1, 4, f);
		d = strlen(m_lpszSongComments);
		fwrite(&d, 1, 4, f);
		fwrite(m_lpszSongComments, 1, d, f);
	}
	// Writing midi cfg
	if (m_dwSongFlags & SONG_EMBEDMIDICFG)
	{
		DWORD d = 0x4944494D;
		fwrite(&d, 1, 4, f);
		d = sizeof(MODMIDICFG);
		fwrite(&d, 1, 4, f);
		fwrite(&m_MidiCfg, 1, sizeof(MODMIDICFG), f);
	}
	// Writing Pattern Names
	if ((m_nPatternNames) && (m_lpszPatternNames))
	{
		DWORD dwLen = m_nPatternNames * MAX_PATTERNNAME;
		while ((dwLen >= MAX_PATTERNNAME) && (!m_lpszPatternNames[dwLen-MAX_PATTERNNAME])) dwLen -= MAX_PATTERNNAME;
		if (dwLen >= MAX_PATTERNNAME)
		{
			DWORD d = 0x4d414e50;
			fwrite(&d, 1, 4, f);
			fwrite(&dwLen, 1, 4, f);
			fwrite(m_lpszPatternNames, 1, dwLen, f);
		}
	}
	// Writing Channel Names
	{
		UINT nChnNames = 0;
		for (UINT inam=0; inam<m_nChannels; inam++)
		{
			if (ChnSettings[inam].szName[0]) nChnNames = inam+1;
		}
		// Do it!
		if (nChnNames)
		{
			DWORD dwLen = nChnNames * MAX_CHANNELNAME;
			DWORD d = 0x4d414e43;
			fwrite(&d, 1, 4, f);
			fwrite(&dwLen, 1, 4, f);
			for (UINT inam=0; inam<nChnNames; inam++)
			{
				fwrite(ChnSettings[inam].szName, 1, MAX_CHANNELNAME, f);
			}
		}
	}
	// Save mix plugins information
	SaveMixPlugins(f);

// -> CODE#0027
// -> DESC="per-instrument volume ramping setup (refered as attack)"
	if(Headers[1]){
		__int16 size;
		__int32 code = 'MPTX';
		// write extension header code
		fwrite(&code, 1, sizeof(__int32), f);
		// write nVolRamp field code
		code = 'VR..';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nVolRamp field size
		size = sizeof(Headers[1]->nVolRamp);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nVolRamp field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nVolRamp, 1, sizeof(USHORT), f);

		//rewbs.instroVSTi: manually write extra codes one by one 
		// write nMixPlug field code
		code = 'MiP.';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nMixPlug field size
		size = sizeof(Headers[1]->nMixPlug);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nMixPlug field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nMixPlug, 1, size, f);

		// write nMidiChannel field code
		code = 'MC..';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nMidiChannel field size
		size = sizeof(Headers[1]->nMidiChannel);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nMidiChannel field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nMidiChannel, 1, size, f);

		// write nMidiProgram field code
		code = 'MP..';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nMidiProgram field size
		size = sizeof(Headers[1]->nMidiProgram);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nMidiProgram field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nMidiProgram, 1, size, f);
		//end rewbs.instroVSTi

		// write wMidiBank field code
		code = 'MB..';
		fwrite(&code, 1, sizeof(__int32), f);
		// write wMidiBank field size
		size = sizeof(Headers[1]->wMidiBank);
		fwrite(&size, 1, sizeof(__int16), f);
		// write wMidiBank field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->wMidiBank, 1, size, f);
		//end rewbs.instroVSTi

		//rewbs.fix36944: write full precision panning, volume and fade.

		// write nPan field code
		code = 'P...';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nPan field size
		size = sizeof(Headers[1]->nPan);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nPan field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nPan, 1, size, f);

		// write nGlobalVol field code
		code = 'GV..';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nGlobalVol field size
		size = sizeof(Headers[1]->nGlobalVol);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nGlobalVol field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nGlobalVol, 1, size, f);

		// write nFadeOut field code
		code = 'FO..';
		fwrite(&code, 1, sizeof(__int32), f);
		// write nFadeOut field size
		size = sizeof(Headers[1]->nFadeOut);
		fwrite(&size, 1, sizeof(__int16), f);
		// write nFadeOut field for each instrument
		for(UINT nins=1; nins<=header.instruments; nins++) if(Headers[nins]) fwrite(&Headers[nins]->nFadeOut, 1, size, f);

		//end rewbs.fix36944

	}
// -! NEW_FEATURE#0027

	{  //Extra song data - Yet Another Hack. 
		__int16 size;
		//Extra song file data
		__int32 code = 'MPTS';
		fwrite(&code, 1, sizeof(__int32), f);
		
		
		code = 'DT..';							//write m_nDefaultTempo field code
		fwrite(&code, 1, sizeof(__int32), f);	
		size = sizeof(m_nDefaultTempo);			//write m_nDefaultTempo field size
		fwrite(&size, 1, sizeof(__int16), f);
		fwrite(&m_nDefaultTempo, 1, size, f);	//write m_nDefaultTempo

		code = 'RPB.';							//write m_nRowsPerBeat field code
		fwrite(&code, 1, sizeof(__int32), f);	
		size = sizeof(m_nRowsPerBeat);			//write m_nRowsPerBeat field size
		fwrite(&size, 1, sizeof(__int16), f);
		fwrite(&m_nRowsPerBeat, 1, size, f);	//write m_nRowsPerBeat

		code = 'RPM.';							//write m_nRowsPerMeasure field code
		fwrite(&code, 1, sizeof(__int32), f);	
		size = sizeof(m_nRowsPerMeasure);		//write m_nRowsPerMeasure field size
		fwrite(&size, 1, sizeof(__int16), f);
		fwrite(&m_nRowsPerMeasure, 1, size, f);	//write m_nRowsPerMeasure

	}


	fclose(f);
	return TRUE;
}

#endif // MODPLUG_NO_FILESAVE
