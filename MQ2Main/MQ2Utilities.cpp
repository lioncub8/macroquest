/*
 * MacroQuest2: The extension platform for EverQuest
 * Copyright (C) 2002-2019 MacroQuest Authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "MQ2Main.h"

#ifdef _DEBUG
#define DBG_SPEW // enable DebugSpew messages in debug builds
#endif

//***************************************************************************
// Function:    DebugSpew
// Description: Outputs text to debugger, usage is same as printf ;)
//***************************************************************************

static void LogToFile(const char* szOutput)
{
	FILE* fOut = nullptr;
	char szFilename[MAX_PATH] = { 0 };

	sprintf_s(szFilename, "%s\\DebugSpew.log", gszLogPath);
	errno_t err = fopen_s(&fOut, szFilename, "at");

	if (err || !fOut)
		return;

#ifdef DBG_CHARNAME
	char Name[256] = "Unknown";
	if (CHARINFO * pCharInfo = GetCharInfo())
	{
		strcpy_s(Name, pCharInfo->Name);
	}
	fprintf(fOut, "%s - ", Name);
#endif

	fprintf(fOut, "%s\r\n", szOutput);
	fclose(fOut);
}

static void DebugSpewImpl(bool always, bool logToFile, const char* szFormat, va_list vaList)
{
	if (!always && gFilterDebug)
		return;

	// _vscprintf doesn't count // terminating '\0'
	int len = _vscprintf(szFormat, vaList) + 1;
	int headerlen = strlen(DebugHeader) + 1;
	size_t theLen = len + headerlen + 32;

	auto out = std::make_unique<char[]>(theLen);
	char* szOutput = out.get();

	strcpy_s(szOutput, theLen, DebugHeader " ");
	vsprintf_s(szOutput + headerlen, theLen - headerlen, szFormat, vaList);

	strcat_s(szOutput, theLen, "\n");
	OutputDebugString(szOutput);

	if (logToFile)
	{
		LogToFile(szOutput);
	}
}

void DebugSpew(const char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);

	DebugSpewImpl(false, false, szFormat, vaList);
}

void DebugSpewAlways(const char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);

	DebugSpewImpl(true, gSpewToFile, szFormat, vaList);
}

void DebugSpewAlwaysFile(const char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);

	DebugSpewImpl(true, true, szFormat, vaList);
}

EQLIB_API void DebugSpewNoFile(const char* szFormat, ...)
{
#ifdef DBG_SPEW
	va_list vaList;
	va_start(vaList, szFormat);

	DebugSpewImpl(true, false, szFormat, vaList);
#endif
}

// Implemented in MQ2PluginHandler.cpp
void PluginsWriteChatColor(const char* Line, int Color, int Filter);

static void WriteChatColorMaybeDeferred(std::unique_ptr<char[]> Ptr, int Color, int Filter)
{
	if (IsMainThread())
	{
		PluginsWriteChatColor(Ptr.get(), Color, Filter);
	}

	// Queue it up to run on the main thread
	PostToMainThread(
		[Ptr = std::shared_ptr<char[]>{ std::move(Ptr) }, Color, Filter]()
	{
		PluginsWriteChatColor(Ptr.get(), Color, Filter);
	});
}

void WriteChatColor(const char* Line, int Color /* = USERCOLOR_DEFAULT */, int Filter /* = 0 */)
{
	// If we're alreadyon the main thread, avoid copying anything and just call
	// straight to PluginsWriteChatColor

	if (IsMainThread())
	{
		PluginsWriteChatColor(Line, Color, Filter);
		return;
	}

	// we're not on the main thread, we need to copy the string and queue up a function
	// to be executed on the main thread.
	size_t length = strlen(Line) + 1;
	std::shared_ptr<char[]> Ptr{ new char[length] };
	strcpy_s(Ptr.get(), length, Line);

	// Queue it up to run on the main thread
	PostToMainThread(
		[Ptr, Color, Filter]()
	{
		PluginsWriteChatColor(Ptr.get(), Color, Filter);
	});
}

void WriteChatf(const char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);

	// _vscprintf doesn't count // terminating '\0'
	int len = _vscprintf(szFormat, vaList) + 1;

	auto out = std::make_unique<char[]>(len);
	char* szOutput = out.get();

	vsprintf_s(szOutput, len, szFormat, vaList);
	WriteChatColor(szOutput);
}

void WriteChatfSafe(const char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);

	// _vscprintf doesn't count // terminating '\0'
	int len = _vscprintf(szFormat, vaList) + 1;

	auto out = std::make_unique<char[]>(len);
	char* szOutput = out.get();

	vsprintf_s(szOutput, len, szFormat, vaList);
	WriteChatColor(szOutput);
}

void WriteChatColorf(const char* szFormat, int color, ...)
{
	va_list vaList;
	va_start(vaList, color);

	// _vscprintf doesn't count // terminating '\0'
	int len = _vscprintf(szFormat, vaList) + 1;

	auto out = std::make_unique<char[]>(len);
	char* szOutput = out.get();

	vsprintf_s(szOutput, len, szFormat, vaList);
	WriteChatColor(szOutput, color);
}

//============================================================================

static void StrReplaceSection(char* szInsert, size_t InsertLen, DWORD Length, const char* szNewString)
{
	DWORD NewLength = (DWORD)strlen(szNewString);
	memmove(&szInsert[NewLength], &szInsert[Length], strlen(&szInsert[Length]) + 1);
	memcpy_s(szInsert, InsertLen - NewLength, szNewString, NewLength);
}

void ConvertCR(char* Text, size_t LineLen)
{
	// not super-efficient but this is only being called at initialization currently.
	while (char* Next = strstr(Text, "\\n"))
	{
		int len = (int)(Next - Text);
		StrReplaceSection(Next, LineLen - len, 2, "\n");
	}
}

void SyntaxError(char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	int len = _vscprintf(szFormat, vaList) + 1;// _vscprintf doesn't count // terminating '\0'
	if (char* szOutput = (char*)LocalAlloc(LPTR, len + 32)) {
		vsprintf_s(szOutput, len + 32, szFormat, vaList);
		WriteChatColor(szOutput, CONCOLOR_YELLOW);
		strcpy_s(gszLastSyntaxError, szOutput);
		LocalFree(szOutput);
	}
}

void MacroError(char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	int len = _vscprintf(szFormat, vaList) + 1;// _vscprintf doesn't count // terminating '\0'
	if (char* szOutput = (char*)LocalAlloc(LPTR, len + 32)) {
		vsprintf_s(szOutput, len + 32, szFormat, vaList);
		WriteChatColor(szOutput, CONCOLOR_RED);
		if (bAllErrorsLog) MacroLog(NULL, "Macro Error");
		if (bAllErrorsLog) MacroLog(NULL, szOutput);
		strcpy_s(gszLastNormalError, szOutput);
		LocalFree(szOutput);
	}
	if (gMacroBlock)
	{
		if (bAllErrorsDumpStack || bAllErrorsFatal)
			DumpStack(nullptr, nullptr);
		if (bAllErrorsFatal) {
			EndMacro((PSPAWNINFO)pLocalPlayer, "");
		}
	}
}

void FatalError(char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	int len = _vscprintf(szFormat, vaList) + 1;// _vscprintf doesn't count // terminating '\0'
	if (char* szOutput = (char*)LocalAlloc(LPTR, len + 32)) {
		vsprintf_s(szOutput, len + 32, szFormat, vaList);
		WriteChatColor(szOutput, CONCOLOR_RED);
		strcpy_s(gszLastNormalError, szOutput);
		if (bAllErrorsLog) MacroLog(NULL, "Fatal Error");
		if (bAllErrorsLog) MacroLog(NULL, szOutput);
		LocalFree(szOutput);
	}

	if (gMacroBlock)
	{
		DumpStack(0, 0);
		EndMacro((PSPAWNINFO)pLocalPlayer, "");
	}
}

void MQ2DataError(char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	int len = _vscprintf(szFormat, vaList) + 1;// _vscprintf doesn't count // terminating '\0'
	if (char* szOutput = (char*)LocalAlloc(LPTR, len + 32)) {
		vsprintf_s(szOutput, len + 32, szFormat, vaList);
		if (gFilterMQ2DataErrors)
			DebugSpew("%s", szOutput);
		else
		{
			WriteChatColor(szOutput, CONCOLOR_RED);
		}
		strcpy_s(gszLastMQ2DataError, szOutput);
		if (bAllErrorsLog) MacroLog(NULL, "Data Error");
		if (bAllErrorsLog) MacroLog(NULL, szOutput);
		LocalFree(szOutput);
	}
	if (gMacroBlock)
	{
		if (bAllErrorsDumpStack || bAllErrorsFatal)
			DumpStack(false, false);
		if (bAllErrorsFatal)
			EndMacro((PSPAWNINFO)pLocalPlayer, "");
	}
}

void FixStringTable()
{
	EQSTRINGTABLE* pTable = (EQSTRINGTABLE*)pStringTable;
	for (DWORD N = 0; N < pTable->Count; N++)
	{
		if (PEQSTRING pStr = pTable->StringItems[N])
		{
			if (char* p = pStr->String)
			{
				while (*p)
					p++;
				p--;
				while (*p == ' ' && p != pStr->String)
				{
					*p = 0;
					p--;
				}
			}
		}
	}
}

// ***************************************************************************
// Function:    GetNextArg
// Description: Returns a pointer to the next argument
// ***************************************************************************
PSTR GetNextArg(PCSTR szLine, DWORD dwNumber, BOOL CSV, char Separator)
{
	PCSTR szNext = szLine;
	BOOL CustomSep = FALSE;
	BOOL InQuotes = FALSE;
	if (Separator != 0) CustomSep = TRUE;

	while (
		((!CustomSep) && (szNext[0] == ' '))
		|| ((!CustomSep) && (szNext[0] == '\t'))
		|| ((CustomSep) && (szNext[0] == Separator))
		|| ((!CustomSep) && (CSV) && (szNext[0] == ','))
		) szNext++;

	if ((INT)dwNumber < 1) return (PSTR)szNext;
	for (dwNumber; dwNumber > 0; dwNumber--) {
		while ((
			((CustomSep) || (szNext[0] != ' '))
			&& ((CustomSep) || (szNext[0] != '\t'))
			&& ((!CustomSep) || (szNext[0] != Separator))
			&& ((CustomSep) || (!CSV) || (szNext[0] != ','))
			&& (szNext[0] != 0)
			)
			|| (InQuotes)
			) {
			if ((szNext[0] == 0) && (InQuotes)) {
				DebugSpew("GetNextArg - No matching quote, returning empty string");
				return (PSTR)szNext;
			}
			if (szNext[0] == '"') InQuotes = !InQuotes;
			szNext++;
		}
		while (
			((!CustomSep) && (szNext[0] == ' '))
			|| ((!CustomSep) && (szNext[0] == '\t'))
			|| ((CustomSep) && (szNext[0] == Separator))
			|| ((!CustomSep) && (CSV) && (szNext[0] == ','))
			) szNext++;
	}
	return (PSTR)szNext;
}

// ***************************************************************************
// Function:    GetArg
// Description: Returns a pointer to the current argument in szDest
// ***************************************************************************
PSTR GetArg(PSTR szDest, PCSTR szSrc, DWORD dwNumber, BOOL LeaveQuotes, BOOL ToParen, BOOL CSV, char Separator, BOOL AnyNonAlphaNum)
{
	if (!szSrc)
		return NULL;
	DWORD i = 0;
	DWORD j = 0;
	BOOL CustomSep = FALSE;
	BOOL InQuotes = FALSE;
	PCSTR szTemp = szSrc;
	ZeroMemory(szDest, MAX_STRING);

	if (Separator != 0) CustomSep = TRUE;

	szTemp = GetNextArg(szTemp, dwNumber - 1, CSV, Separator);

	while ((
		((CustomSep) || (szTemp[i] != ' '))
		&& ((CustomSep) || (szTemp[i] != '\t'))
		&& ((CustomSep) || (!CSV) || (szTemp[i] != ','))
		&& ((!CustomSep) || (szTemp[i] != Separator))
		&& ((!AnyNonAlphaNum) || (
		(szTemp[i] >= '0' && szTemp[i] <= '9') ||
			(szTemp[i] >= 'a' && szTemp[i] <= 'z') ||
			(szTemp[i] >= 'A' && szTemp[i] <= 'Z') ||
			(szTemp[i] == '_')
			))
		&& (szTemp[i] != 0)
		&& ((!ToParen) || (szTemp[i] != ')'))
		)
		|| (InQuotes)
		) {
		if ((szTemp[i] == 0) && (InQuotes)) {
			DebugSpew("GetArg - No matching quote, returning entire string");
			DebugSpew("Source = %s", szSrc);
			DebugSpew("Dest = %s", szDest);
			return szDest;
		}
		if (szTemp[i] == '"') {
			InQuotes = !InQuotes;
			if (LeaveQuotes) {
				szDest[j] = szTemp[i];
				j++;
			}
		}
		else {
			szDest[j] = szTemp[i];
			j++;
		}
		i++;
	}
	if ((ToParen) && (szTemp[i] == ')')) szDest[j] = ')';
	//DebugSpew("GetArg - Arg%d from '%s' = '%s'",dwNumber,szTemp,szDest);

	return szDest;
}

char* GetEQPath(char* szBuffer, size_t len)
{
	GetModuleFileName(NULL, szBuffer, MAX_STRING);
	char* pSearch = 0;
	_strlwr_s(szBuffer, len);
	if (pSearch = strstr(szBuffer, "\\wineq\\"))
		* pSearch = 0;
	else if (pSearch = strstr(szBuffer, "\\testeqgame.exe"))
		* pSearch = 0;
	else if (pSearch = strstr(szBuffer, "\\eqgame.exe"))
		* pSearch = 0;
	return szBuffer;
}

void ConvertItemTags(CXStr& cxstr, BOOL Tag)
{
	// FIXME why asm?
	__asm {
		push ecx;
		push eax;
		push[Tag];
		push[cxstr];
		call[EQADDR_CONVERTITEMTAGS];
		pop ecx;
		pop ecx;
		pop eax;
		pop ecx;
	};
}

#define InsertColor(text,color) sprintf(text,"<c \"#%06X\">",color);TotalColors++; 
#define InsertColorSafe(text,len,color) sprintf_s(text, len,"<c \"#%06X\">",color);TotalColors++; 
#define InsertStopColor(text)   sprintf(text,"</c>");TotalColors--; 
#define InsertStopColorSafe(text,len)   sprintf_s(text, len, "</c>");TotalColors--; 

void StripMQChat(const char* in, char* out)
{
	//DebugSpew("StripMQChat(%s)",in);
	int i = 0;
	int o = 0;
	while (in[i])
	{
		if (in[i] == '\a')
		{
			i++;
			if (in[i] == '-')
			{
				// skip 1 after -
				i++;
			}
			else if (in[i] == '#')
			{
				// skip 6 after #
				i += 6;
			}
		}
		else if (in[i] == '\n')
		{
		}
		else
			out[o++] = in[i];
		i++;
	}
	out[o] = 0;
	//DebugSpew("StripMQChat=>(%s)",out);
}

bool ReplaceSafely(char** out, DWORD* pchar_out_string_position, char chr, DWORD maxlen)
{
	if ((*pchar_out_string_position) + 1 > maxlen)
		return false;
	(*out)[(*pchar_out_string_position)++] = chr;
	return true;
}

DWORD MQToSTML(const char* in, char* out, DWORD maxlen, DWORD ColorOverride)
{
	//DebugSpew("MQToSTML(%s)",in);
	// 1234567890123
	// <c "#123456">
	//char szCmd[MAX_STRING] = { 0 };
	//strcpy_s(szCmd, out);
	int outlen = maxlen;
	if (maxlen > 14)
		maxlen -= 14; // make room for this: <c "#123456">
	DWORD pchar_in_string_position = 0;
	DWORD pchar_out_string_position = 0;
	BOOL bFirstColor = false;
	BOOL bNBSpace = false;
	ColorOverride &= 0xFFFFFF;
	DWORD CurrentColor = ColorOverride;
	int TotalColors = 0; // this MUST be signed.
	pchar_out_string_position += InsertColorSafe(&out[pchar_out_string_position], outlen - pchar_out_string_position, CurrentColor);

	while (in[pchar_in_string_position] != 0 && pchar_out_string_position < maxlen)
	{
		if (in[pchar_in_string_position] == ' ')
		{
			if (bNBSpace) {
				if (!ReplaceSafely(&out, &pchar_out_string_position, '&', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'N', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'B', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'S', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'P', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, ';', maxlen))
					break;
			}
			else {
				if (!ReplaceSafely(&out, &pchar_out_string_position, ' ', maxlen))
					break;
			}
			bNBSpace = 1;
		}
		else
		{
			bNBSpace = 0;
			switch (in[pchar_in_string_position])
			{
			case '\a':
				// HANDLE COLOR
				bFirstColor = true;
				pchar_in_string_position++;
				if (in[pchar_in_string_position] == 'x')
				{
					CurrentColor = -1;
					pchar_out_string_position += InsertStopColorSafe(&out[pchar_out_string_position], outlen - pchar_out_string_position);
					if (pchar_out_string_position >= maxlen)
						break;
				}
				else
					if (in[pchar_in_string_position] == '#')
					{
						pchar_in_string_position++;
						char temp[7];
						for (int x = 0; x < 6; x++)
						{
							temp[x] = in[pchar_in_string_position++];
						}
						pchar_in_string_position--;
						temp[6] = 0;
						CurrentColor = -1;
						//pchar_out_string_position += sprintf_s(&out[pchar_out_string_position],outlen-pchar_out_string_position, "<c \"#%s\">", &temp[0]);
						pchar_out_string_position += sprintf_s(&out[pchar_out_string_position], outlen - pchar_out_string_position, "<c \"#%s\">", &temp[0]);
						TotalColors++;
						if (pchar_out_string_position >= maxlen)
							break;
					}
					else
					{
						bool Dark = false;
						if (in[pchar_in_string_position] == '-')
						{
							Dark = true;
							pchar_in_string_position++;
						}
						int LastColor = CurrentColor;
						switch (in[pchar_in_string_position])
						{
						case 'y': // yellow (green/red)
							if (Dark)
								CurrentColor = 0x999900;
							else
								CurrentColor = 0xFFFF00;
							break;
						case 'o': // orange (green/red)
							if (Dark)
								CurrentColor = 0x996600;
							else
								CurrentColor = 0xFF9900;
							break;
						case 'g': // green   (green)
							if (Dark)
								CurrentColor = 0x009900;
							else
								CurrentColor = 0x00FF00;
							break;
						case 'u': // blue   (blue)
							if (Dark)
								CurrentColor = 0x000099;
							else
								CurrentColor = 0x0000FF;
							break;
						case 'r': // red     (red)
							if (Dark)
								CurrentColor = 0x990000;
							else
								CurrentColor = 0xFF0000;
							break;
						case 't': // teal (blue/green)
							if (Dark)
								CurrentColor = 0x009999;
							else
								CurrentColor = 0x00FFFF;
							break;
						case 'b': // black   (none)
							CurrentColor = 0x000000;
							break;
						case 'm': // magenta (blue/red)
							if (Dark)
								CurrentColor = 0x990099;
							else
								CurrentColor = 0xFF00FF;
							break;
						case 'p': // purple (blue/red)
							if (Dark)
								CurrentColor = 0x660099;
							else
								CurrentColor = 0x9900FF;
							break;
						case 'w': // white   (all)
							if (Dark)
								CurrentColor = 0x999999;
							else
								CurrentColor = 0xFFFFFF;
							break;
						}
						if ((int)CurrentColor != LastColor)
						{
							//pchar_out_string_position += InsertColor(&out[pchar_out_string_position], CurrentColor);
							pchar_out_string_position += InsertColorSafe(&out[pchar_out_string_position], outlen - pchar_out_string_position, CurrentColor);
							if (pchar_out_string_position >= maxlen)
								break;
						}
					}
				break;
			case '&':
				if (!ReplaceSafely(&out, &pchar_out_string_position, '&', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'A', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'M', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'P', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, ';', maxlen))
					break;
				break;
			case '%':
				if (!ReplaceSafely(&out, &pchar_out_string_position, '&', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'P', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'C', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'T', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, ';', maxlen))
					break;
				break;
			case '<':
				if (!ReplaceSafely(&out, &pchar_out_string_position, '&', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'L', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'T', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, ';', maxlen))
					break;
				break;
			case '>':
				if (!ReplaceSafely(&out, &pchar_out_string_position, '&', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'G', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'T', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, ';', maxlen))
					break;
				break;
			case '"':
				if (!ReplaceSafely(&out, &pchar_out_string_position, '&', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'Q', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'U', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'O', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'T', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, ';', maxlen))
					break;
				break;
			case '\n':
				if (!ReplaceSafely(&out, &pchar_out_string_position, '<', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'B', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, 'R', maxlen))
					break;
				if (!ReplaceSafely(&out, &pchar_out_string_position, '>', maxlen))
					break;
				break;
			default:
				out[pchar_out_string_position++] = in[pchar_in_string_position];
				//szCmd[pchar_out_string_positions++] = in[pchar_in_string_positions];
				break;
			}
		}
		if (pchar_out_string_position >= maxlen)
			break;
		else
			pchar_in_string_position++;
	}
	if (pchar_out_string_position > maxlen) {
		pchar_out_string_position = maxlen;
	}
	for (TotalColors; TotalColors > 0;)
	{
		//pchar_out_string_position += InsertStopColor(&out[pchar_out_string_position]);
		pchar_out_string_position += InsertStopColorSafe(&out[pchar_out_string_position], outlen - pchar_out_string_position);
	}
	out[pchar_out_string_position++] = 0;
	//szCmd[pchar_out_string_positions++] = 0;
	return pchar_out_string_position;
}

const char* GetFilenameFromFullPath(const char* Filename)
{
	while (Filename && strstr(Filename, "\\"))
		Filename = strstr(Filename, "\\") + 1;
	return Filename;
}

char* GetSubFromLine(int Line, char* szSub, size_t Sublen)
{
	std::map<int, MQMacroLine>::reverse_iterator ri(gMacroBlock->Line.find(Line));
	for (; ri != gMacroBlock->Line.rend(); ri++) {
		//while (pLine != NULL) {
		if (!_strnicmp(ri->second.Command.c_str(), "sub ", 4)) {
			strcpy_s(szSub, Sublen, ri->second.Command.c_str() + 4);
			return szSub;
		}
		//pLine = pLine->pPrev;
	}
	strcpy_s(szSub, Sublen, "NULL");
	return szSub;
}

BOOL CompareTimes(char* RealTime, char* ExpectedTime)
{
	//Match everything except seconds
	//Format is: WWW MMM DD hh:mm:ss YYYY
	//           0123456789012345678901234
	//                     1         2
	if (!_strnicmp(RealTime, ExpectedTime, 17) &&
		!_strnicmp(RealTime + 19, ExpectedTime + 19, 5))
		return TRUE;
	return FALSE;
}

void AddFilter(const char* szFilter, int Length, bool& pEnabled)
{
	MQFilter* New = new MQFilter(szFilter, Length, pEnabled);

	New->pNext = gpFilters;
	gpFilters = New;
}

void DefaultFilters()
{
	AddFilter("You have become better at ", 26, gFilterSkillsIncrease);
	AddFilter("You lacked the skills to fashion the items together.", -1, gFilterSkillsAll);
	AddFilter("You have fashioned the items together to create something new!", -1, gFilterSkillsAll);
	AddFilter("You have fashioned the items together to create an alternate product.", -1, gFilterSkillsAll);
	AddFilter("You can no longer advance your skill from making this item.", -1, gFilterSkillsAll);
	AddFilter("You no longer have a target.", -1, gFilterTarget);
	AddFilter("You give ", 9, gFilterMoney);
	AddFilter("You receive ", 12, gFilterMoney);
	AddFilter("You are encumbered", 17, gFilterEncumber);
	AddFilter("You are no longer encumbered", 27, gFilterEncumber);
	AddFilter("You are low on drink", 19, gFilterFood);
	AddFilter("You are low on food", 18, gFilterFood);
	AddFilter("You are out of drink", 19, gFilterFood);
	AddFilter("You are out of food", 18, gFilterFood);
	AddFilter("You and your mount are thirsty.", -1, gFilterFood);
	AddFilter("You and your mount are hungry.", -1, gFilterFood);
	AddFilter("You are hungry", 13, gFilterFood);
	AddFilter("You are thirsty", 14, gFilterFood);
	AddFilter("You take a bite out of", 22, gFilterFood);
	AddFilter("You take a bite of", 18, gFilterFood);
	AddFilter("You take a drink from", 21, gFilterFood);
	AddFilter("Ahhh. That was tasty.", -1, gFilterFood);
	AddFilter("Ahhh. That was refreshing.", -1, gFilterFood);
	AddFilter("Chomp, chomp, chomp...", 22, gFilterFood);
	AddFilter("Glug, glug, glug...", 19, gFilterFood);
	AddFilter("You could not possibly eat any more, you would explode!", -1, gFilterFood);
	AddFilter("You could not possibly drink any more, you would explode!", -1, gFilterFood);
	AddFilter("You could not possibly consume more alcohol or become more intoxicated!", -1, gFilterFood);
}

char* ConvertHotkeyNameToKeyName(char* szName, size_t Namelen)
{
	if (!_stricmp(szName, "EQUALSIGN"))
		strcpy_s(szName, Namelen, "=");
	if (!_stricmp(szName, "SEMICOLON"))
		strcpy_s(szName, Namelen, ";");
	if (!_stricmp(szName, "LEFTBRACKET"))
		strcpy_s(szName, Namelen, "[");
	return szName;
}

// ***************************************************************************
// Function:    GetFullZone
// Description: Returns a full zone name from a short name
// ***************************************************************************
char* GetFullZone(DWORD ZoneID)
{
	ZoneID &= 0x7FFF;
	if (!ppWorldData || (ppWorldData && !pWorldData))
		return NULL;
	if (ZoneID >= MAX_ZONES)
		return "UNKNOWN_ZONE";
	PZONELIST pZone = ((PWORLDDATA)pWorldData)->ZoneArray[ZoneID];
	if (pZone)
		return pZone->LongName;
	else
		return "UNKNOWN_ZONE";
}
// ***************************************************************************
// Function:    GetShortZone
// Description: Returns a short zone name from a ZoneID
// ***************************************************************************
char* GetShortZone(DWORD ZoneID)
{
	ZoneID &= 0x7FFF;
	if (!ppWorldData || (ppWorldData && !pWorldData))
		return NULL;
	if (ZoneID >= MAX_ZONES)
		return "UNKNOWN_ZONE";
	PZONELIST pZone = ((PWORLDDATA)pWorldData)->ZoneArray[ZoneID];
	if (pZone)
		return pZone->ShortName;
	else
		return "UNKNOWN_ZONE";
}
// ***************************************************************************
// Function:    GetZoneID
// Description: Returns a ZoneID from a short or long zone name
// ***************************************************************************

int GetZoneID(const char* ZoneShortName)
{
	PZONELIST pZone = nullptr;

	if (!ppWorldData || (ppWorldData && !pWorldData))
		return (DWORD)-1;
	for (int nIndex = 0; nIndex < MAX_ZONES; nIndex++) {
		pZone = ((PWORLDDATA)pWorldData)->ZoneArray[nIndex];
		if (pZone) {
			if (!_stricmp(pZone->ShortName, ZoneShortName)) {
				return nIndex;
			}
			else if (!_stricmp(pZone->LongName, ZoneShortName)) {
				return nIndex;
			}
		}
	}
	return -1;
}

// ***************************************************************************
// Function:    GetGameTime
// Description: Returns Current Game Time
// ***************************************************************************
void GetGameTime(int* Hour, int* Minute, int* Night)
{
	int eqHour = 0;
	int eqMinute = 0;
	if (!ppWorldData || (ppWorldData && !pWorldData))
		return;
	eqHour = ((PWORLDDATA)pWorldData)->Hour - 1; // Midnight = 1 in EQ time
	eqMinute = ((PWORLDDATA)pWorldData)->Minute;
	if (Hour)* Hour = eqHour;
	if (Minute)* Minute = eqMinute;
	if (Night)* Night = ((eqHour < 7) || (eqHour > 18));//?TRUE:FALSE; // already handled by operators
}

// ***************************************************************************
// Function:    GetGameDate
// Description: Returns Current Game Time
// ***************************************************************************
void GetGameDate(int* Month, int* Day, int* Year)
{
	if (!ppWorldData || (ppWorldData && !pWorldData))
		return;
	if (Month)* Month = ((PWORLDDATA)pWorldData)->Month;
	if (Day)* Day = ((PWORLDDATA)pWorldData)->Day;
	if (Year)* Year = ((PWORLDDATA)pWorldData)->Year;
}

int GetLanguageIDByName(char* SzName)
{
	if (!_stricmp(SzName, "Common")) return 1;
	if (!_stricmp(SzName, "Common Tongue")) return 1;
	if (!_stricmp(SzName, "Barbarian")) return 2;
	if (!_stricmp(SzName, "Erudian")) return 3;
	if (!_stricmp(SzName, "Elvish")) return 4;
	if (!_stricmp(SzName, "Dark Elvish")) return 5;
	if (!_stricmp(SzName, "Dwarvish")) return 6;
	if (!_stricmp(SzName, "Troll")) return 7;
	if (!_stricmp(SzName, "Ogre")) return 8;
	if (!_stricmp(SzName, "Gnomish")) return 9;
	if (!_stricmp(SzName, "Halfling")) return 10;
	if (!_stricmp(SzName, "Thieves Cant")) return 11;
	if (!_stricmp(SzName, "Old Erudian")) return 12;
	if (!_stricmp(SzName, "Elder Elvish")) return 13;
	if (!_stricmp(SzName, "Froglok")) return 14;
	if (!_stricmp(SzName, "Goblin")) return 15;
	if (!_stricmp(SzName, "Gnoll")) return 16;
	if (!_stricmp(SzName, "Combine Tongue")) return 17;
	if (!_stricmp(SzName, "Elder Tier'Dal")) return 18;
	if (!_stricmp(SzName, "Lizardman")) return 19;
	if (!_stricmp(SzName, "Orcish")) return 20;
	if (!_stricmp(SzName, "Faerie")) return 21;
	if (!_stricmp(SzName, "Dragon")) return 22;
	if (!_stricmp(SzName, "Elder Dragon")) return 23;
	if (!_stricmp(SzName, "Dark Speech")) return 24;
	if (!_stricmp(SzName, "Vah Shir")) return 25;
	return -1;
}

int GetCurrencyIDByName(char* szName)
{
	if (!_stricmp(szName, "Doubloons")) return ALTCURRENCY_DOUBLOONS;  // 0XA
	if (!_stricmp(szName, "Orux")) return ALTCURRENCY_ORUX; //0XB
	if (!_stricmp(szName, "Phosphenes")) return ALTCURRENCY_PHOSPHENES; //0XC
	if (!_stricmp(szName, "Phosphites")) return ALTCURRENCY_PHOSPHITES; //0XD
	if (!_stricmp(szName, "Faycitum")) return ALTCURRENCY_FAYCITES; //0XE
	if (!_stricmp(szName, "Chronobines")) return ALTCURRENCY_CHRONOBINES; //0XF
	if (!_stricmp(szName, "Silver Tokens")) return ALTCURRENCY_SILVERTOKENS; //0X10
	if (!_stricmp(szName, "Gold Tokens")) return ALTCURRENCY_GOLDTOKENS; //0X11
	if (!_stricmp(szName, "McKenzie's Special Brew")) return ALTCURRENCY_MCKENZIE; //0X12
	if (!_stricmp(szName, "Bayle Marks")) return ALTCURRENCY_BAYLE; //0X13   
	if (!_stricmp(szName, "Tokens of Reclamation")) return ALTCURRENCY_RECLAMATION; //0X14
	if (!_stricmp(szName, "Brellium Tokens")) return ALTCURRENCY_BRELLIUM; //0X15
	if (!_stricmp(szName, "Dream Motes")) return ALTCURRENCY_MOTES; //0X16
	if (!_stricmp(szName, "Rebellion Chits")) return ALTCURRENCY_REBELLIONCHITS; //0X17
	if (!_stricmp(szName, "Diamond Coins")) return ALTCURRENCY_DIAMONDCOINS; //0X18
	if (!_stricmp(szName, "Bronze Fiats")) return ALTCURRENCY_BRONZEFIATS; //0X19
	if (!_stricmp(szName, "Expedient Delivery Vouchers")) return ALTCURRENCY_VOUCHER; //0x1a
	if (!_stricmp(szName, "Velium Shards")) return ALTCURRENCY_VELIUMSHARDS; //0X1b
	if (!_stricmp(szName, "Crystallized Fear")) return ALTCURRENCY_CRYSTALLIZEDFEAR; //0X1c
	if (!_stricmp(szName, "Shadowstones")) return ALTCURRENCY_SHADOWSTONES; //0X1d
	if (!_stricmp(szName, "Dreadstones")) return ALTCURRENCY_DREADSTONES; //0X1e
	if (!_stricmp(szName, "Marks of Valor")) return ALTCURRENCY_MARKSOFVALOR; //0X1F
	if (!_stricmp(szName, "Medals of Heroism")) return ALTCURRENCY_MEDALSOFHEROISM; //0X20   
	if (!_stricmp(szName, "Commemorative Coins")) return ALTCURRENCY_COMMEMORATIVE_COINS; //0X21 
	if (!_stricmp(szName, "Fists of Bayle")) return ALTCURRENCY_FISTSOFBAYLE; //0X22
	if (!_stricmp(szName, "Nobles")) return ALTCURRENCY_NOBLES; //0X23
	if (!_stricmp(szName, "Arx Energy Crystals")) return ALTCURRENCY_ENERGYCRYSTALS; //0X24
	if (!_stricmp(szName, "Pieces of Eight")) return ALTCURRENCY_PIECESOFEIGHT; //0X25
	return -1;
}

PSPELL GetSpellBySpellGroupID(LONG dwSpellGroupID)
{
	if (ppSpellMgr) {
		for (DWORD dwSpellID = 0; dwSpellID < TOTAL_SPELL_COUNT; dwSpellID++) {
			if (PSPELL pSpell = GetSpellByID(dwSpellID)) {
				if (pSpell->ID > 0) {
					if (pSpell->SpellGroup == dwSpellGroupID) {
						return pSpell;
					}
				}
			}
		}
	}
	return NULL;
}

char* GetSpellNameBySpellGroupID(LONG dwSpellID)
{
	PSPELL pSpell = GetSpellBySpellGroupID(abs(dwSpellID));
	if (pSpell && pSpell->Name && pSpell->Name[0] != '\0') {
		return pSpell->Name;
	}
	return "Unknown Spell";
}

char* GetSpellNameByID(LONG dwSpellID)
{
	long absedspellid = abs(dwSpellID);
	if (ppSpellMgr && absedspellid != 0 && absedspellid != -1 && absedspellid < TOTAL_SPELL_COUNT) {
		PSPELL pSpell = GetSpellByID(absedspellid);
		if (pSpell && pSpell->Name && pSpell->Name[0] != '\0') {
			return pSpell->Name;
		}
	}
	return "Unknown Spell";
}

struct SpellCompare
{
	std::map<int, SPELL*> Duplicates;
};

std::map<std::string, std::map<std::string, SpellCompare>> s_spellNameMap;
std::map<int, int> s_triggeredSpells;
std::mutex s_initializeSpellsMutex;


bool IsRecursiveEffect2(int spa)
{
	switch (spa)
	{
	case 374:
	case 475:
	case 340:
	case 470:
	case 469:
		return true;
	}
	return false;
}

void PopulateTriggeredmap(SPELL* pSpell)
{
	if (pSpell->CannotBeScribed == 1)
		return;

	int slots = GetSpellNumEffects(pSpell);

	for (int i = 0; i < slots; i++)
	{
		int attrib = GetSpellAttrib(pSpell, i);

		if (IsRecursiveEffect2(attrib))
		{
			if (int triggeredSpellId = GetSpellBase2(pSpell, i))
			{
				s_triggeredSpells[triggeredSpellId] = pSpell->ID;
			}
		}
	}
}

SPELL* GetSpellParent(int id)
{
	auto iter = s_triggeredSpells.find(id);
	if (iter != s_triggeredSpells.end())
	{
		return GetSpellByID(iter->second);
	}

	return nullptr;
}

void PopulateSpellMap()
{
	std::scoped_lock lock(s_initializeSpellsMutex);

	gbSpelldbLoaded = false;
	s_triggeredSpells.clear();
	s_spellNameMap.clear();

	for (int dwSpellID = 0; dwSpellID < TOTAL_SPELL_COUNT; dwSpellID++)
	{
		if (SPELL * pSpell = pSpellMgr->Spells[dwSpellID])
		{
			if (pSpell->Name[0] != '\0')
			{
				PopulateTriggeredmap(pSpell);

				std::string lowname = pSpell->Name;
				MakeLower(lowname);

				std::string threelow = lowname;
				threelow.erase(3);

				s_spellNameMap[threelow][lowname].Duplicates[dwSpellID] = pSpell;
			}
		}
	}

	gbSpelldbLoaded = true;
}

DWORD CALLBACK InitializeMQ2SpellDb(void* pData)
{
	int state = reinterpret_cast<int>(pData);

	switch (state)
	{
	case 1: WriteChatf("Initializing SpellMap from SetGameState."); break;
	case 2: WriteChatf("Initializing SpellMap from GetSpellByName."); break;
	default: WriteChatf("Initializing SpellMap. (%d)", state); break;
	}

	while (gGameState != GAMESTATE_CHARSELECT && gGameState != GAMESTATE_INGAME)
	{
		Sleep(10);
	}

	while (pSpellMgr && (!pSpellMgr->Spells || (pSpellMgr->Spells && !pSpellMgr->Spells[TOTAL_SPELL_COUNT - 1])))
	{
		Sleep(10);
	}

	// ok everything checks out lets fill our own map with spells
	PopulateSpellMap();

	switch (state)
	{
	case 1: WriteChatf("SpellMap Initialized from SetGameState."); break;
	case 2: WriteChatf("SpellMap Initialized from GetSpellByName."); break;
	default: WriteChatf("SpellMap Initialized. (%d)", state); break;
	}

	ghInitializeSpellDbThread = nullptr;
	return 0;
}

bool IsSpellClassUsable(SPELL* pSpell)
{
	for (int index = Warrior; index <= Berserker; index++)
	{
		if (pSpell->ClassLevel[index] == 255 || pSpell->ClassLevel[index] == 127) {
			continue;
		}
		else
		{
			return true;
		}
	}

	return false;
}

SPELL* GetSpellByName(const char* szName)
{
	// PSPELL GetSpellByName(char* NameOrID)
	// This function now accepts SpellID as an argument as well as SpellName
	// /echo ${Spell[Concussive Burst].Level}
	// /echo ${Spell[Nature's Serenity].Level}

	if (ppSpellMgr == nullptr) // no spellMgr offset?
		return nullptr;
	if (szName == nullptr)     // no spell name?
		return nullptr;

	if (gbSpelldbLoaded == false)
	{
		InitializeMQ2SpellDb((void*)2);

		if (gbSpelldbLoaded == false)
		{
			return nullptr;
		}
	}

	std::scoped_lock lock(s_initializeSpellsMutex);

	if (szName[0] >= '0' && szName[0] <= '9')
	{
		return GetSpellByID(abs(atoi(szName)));
	}

	// is this even necessary?
	CHARINFO2* profile = GetCharInfo2();
	if (!profile)
		return nullptr;

	std::string lowname = szName;
	if (lowname.size() < 3 || s_spellNameMap.empty())
		return nullptr;

	MakeLower(lowname);

	std::string threelow = lowname;
	threelow.erase(3);

	// look up threelow
	auto iter = s_spellNameMap.find(threelow);
	if (iter == s_spellNameMap.end())
		return nullptr;

	// look up lowname
	std::map<std::string, SpellCompare>& spellLookup = iter->second;
	auto iter2 = spellLookup.find(lowname);
	if (iter2 == spellLookup.end())
		return nullptr;

	SpellCompare& comp = iter2->second;
	if (comp.Duplicates.empty())
		return nullptr;

	SPELL* pSpell = comp.Duplicates.begin()->second;
	if (comp.Duplicates.size() == 1)
	{
		return pSpell;
	}

	int highestclasslevel = 0;
	int classlevel = 0;
	int playerclass = profile->Class;
	int currlevel = profile->Level;

	if (playerclass && playerclass >= Warrior && playerclass <= Berserker)
	{
		for (auto& duplicate : iter2->second.Duplicates)
		{
			if (SPELL * dupeSpell = duplicate.second)
			{
				classlevel = dupeSpell->ClassLevel[playerclass];

				if (classlevel <= currlevel && highestclasslevel < classlevel)
				{
					highestclasslevel = classlevel;
					pSpell = dupeSpell;
				}
			}
		}
	}

	if (highestclasslevel == 0)
	{
		// if we got here, the spell the user is after isnt one his character can cast, so
		// we will have to roll through it again and see if its usable by any other class

		for (auto& duplicate : iter2->second.Duplicates)
		{
			SPELL* dupeSpell = duplicate.second;
			if (dupeSpell && IsSpellClassUsable(dupeSpell))
			{
				pSpell = dupeSpell;
			}
		}
	}

	return nullptr;
}
//This wrapper is here to deal with older plugins and to preserve bacwards compatability with older clients (emu)
ALTABILITY* GetAAByIdWrapper(int nAbilityId, int playerLevel)
{
	return pAltAdvManager->GetAAById(nAbilityId, playerLevel);
}

SPELL* GetSpellByAAName(const char* szName)
{
	int level = -1;

	if (SPAWNINFO * pMe = (SPAWNINFO*)pLocalPlayer)
	{
		level = pMe->Level;
	}

	for (int nAbility = 0; nAbility < NUM_ALT_ABILITIES; nAbility++)
	{
		if (ALTABILITY * pAbility = GetAAByIdWrapper(nAbility, level))
		{
			if (pAbility->SpellID != -1)
			{
				if (const char* pName = pCDBStr->GetString(pAbility->nName, eAltAbilityName))
				{
					if (!_stricmp(szName, pName))
					{
						if (SPELL * psp = GetSpellByID(pAbility->SpellID))
						{
							return psp;
						}
					}
				}
			}
		}
	}

	return nullptr;
}

int GetSpellDuration(SPELL* pSpell, SPAWNINFO* pSpawn)
{
	switch (pSpell->DurationType)
	{
	case 0:
		return 0;
	case 1:
	case 6:
		return std::min<unsigned int>((unsigned int)ceil(double(pSpawn->Level) / 2), pSpell->DurationCap);
	case 3:
	case 4:
	case 11:
	case 12:
	case 15:
		if (pSpell->DurationCap) {
			return (pSpell->DurationCap);
		}
		else {
			return (pSpell->DurationType * 10);
		}
	case 2:
		return std::min<unsigned int>((unsigned int)ceil(double(pSpawn->Level) * 0.6), pSpell->DurationCap);
	case 5:
		return 3;
	case 7:
		return std::min<unsigned int>(pSpawn->Level, pSpell->DurationCap ? pSpell->DurationCap : pSpawn->Level);
	case 8:
		return std::min<unsigned int>(pSpawn->Level + 10, pSpell->DurationCap);
	case 9:
		return std::min<unsigned int>(pSpawn->Level * 2 + 10, pSpell->DurationCap);
	case 10:
		return std::min<unsigned int>(pSpawn->Level * 3 + 10, pSpell->DurationCap);
	case 13:
		return pSpell->DurationCap * 6 / 10;
	case 50:
		return -1;
	case 3600:
		return 6000;
	default:
		return -2;
	}
}

DWORD GetDeityTeamByID(DWORD DeityID)
{
	switch (DeityID) {
	case DEITY_ErollisiMarr:
	case DEITY_MithanielMarr:
	case DEITY_RodcetNife:
	case DEITY_Quellious:
	case DEITY_Tunare:
		return 1;
	case DEITY_BrellSerilis:
	case DEITY_Bristlebane:
	case DEITY_Karana:
	case DEITY_Prexus:
	case DEITY_SolusekRo:
	case DEITY_TheTribunal:
	case DEITY_Veeshan:
		return 2;
	case DEITY_Bertoxxulous:
	case DEITY_CazicThule:
	case DEITY_Innoruuk:
	case DEITY_RallosZek:
		return 3;
	default:
		return 0;
	}
}

char* GetGuildByID(int64_t GuildID)
{
	if (GuildID == 0 || GuildID == -1)
		return 0;

	if (char* thename = pGuild->GetGuildName(GuildID)) {
		if (!_stricmp(thename, "Unknown Guild"))
			return 0;
		return thename;
	}
	return 0;
}

int64_t GetGuildIDByName(char* szGuild)
{
	return pGuild->GetGuildIndex(szGuild);
}

const char* GetLightForSpawn(PSPAWNINFO pSpawn)
{
	BYTE Light = pSpawn->Light;
	if (Light > LIGHT_COUNT) Light = 0;
	return szLights[Light];
}

// ***************************************************************************
// Function:    DistanceToSpawn3D
// Description: Return the distance between two spawns, including Z
// ***************************************************************************
float DistanceToSpawn3D(PSPAWNINFO pChar, PSPAWNINFO pSpawn)
{
	float X = pChar->X - pSpawn->X;
	float Y = pChar->Y - pSpawn->Y;
	float Z = pChar->Z - pSpawn->Z;
	return sqrtf(X * X + Y * Y + Z * Z);
}

// ***************************************************************************
// Function:    DistanceToSpawn
// Description: Return the distance between two spawns
// ***************************************************************************
float EstimatedDistanceToSpawn(PSPAWNINFO pChar, PSPAWNINFO pSpawn)
{
	float RDistance = DistanceToSpawn(pChar, pSpawn);
	float X = pChar->X - (pSpawn->X + pSpawn->SpeedX * RDistance);
	float Y = pChar->Y - (pSpawn->Y + pSpawn->SpeedY * RDistance);
	return sqrtf(X * X + Y * Y);
}

// ***************************************************************************
// Function:    ConColor
// Description: Returns the con color for a spawn's level
// ***************************************************************************
DWORD ConColor(SPAWNINFO* pSpawn)
{
	SPAWNINFO* pChar = (SPAWNINFO*)pLocalPlayer;
	if (!pChar)
		return CONCOLOR_WHITE; // its you

	switch (pCharData->GetConLevel((PlayerClient*)pSpawn))
	{
	case 0:
	case 1:
		return CONCOLOR_GREY;
	case 2:
		return CONCOLOR_GREEN;
	case 3:
		return CONCOLOR_LIGHTBLUE;
	case 4:
		return CONCOLOR_BLUE;
	case 5:
		return CONCOLOR_WHITE;
	case 6:
		return CONCOLOR_YELLOW;
	case 7:
		return CONCOLOR_RED;
	default:
		return COLOR_PURPLE;
	}
}

CONTENTS* GetEnviroContainer()
{
	if (!pContainerMgr)
		return nullptr;

	if (!pContainerMgr->pWorldContainer.pObject)
		return nullptr;

	return pContainerMgr->pWorldContainer.pObject;
}

CContainerWnd* FindContainerForContents(CONTENTS* pContents)
{
	if (!pContainerMgr)
		return nullptr;

	for (int j = 0; j < MAX_CONTAINERS; j++)
	{
		if (pContainerMgr->pContainerWnds[j] && pContainerMgr->pContainerWnds[j]->pContents == pContents)
			return pContainerMgr->pContainerWnds[j];
	}

	return nullptr;
}

// ***************************************************************************
// FindSpeed(PSPAWNINFO) - Used to find the speed of a Spawn taking a mount into
//                               consideration.
// ***************************************************************************

float FindSpeed(SPAWNINFO* pSpawn)
{
	SPAWNINFO* pMount = nullptr;
	float fRunSpeed = 0;
	pMount = FindMount(pSpawn);

	if (pMount)
		if (!fRunSpeed)
			fRunSpeed = pMount->SpeedRun * 10000 / 70;

	return fRunSpeed;
}

void GetItemLinkHash(CONTENTS* Item, char* Buffer, size_t BufferSize)
{
	((EQ_Item*)Item)->CreateItemTagString(Buffer, BufferSize, true);
}

bool GetItemLink(CONTENTS* Item, char* Buffer, size_t BufferSize, BOOL Clickable)
{
	char hash[MAX_STRING] = { 0 };
	bool retVal = false;
	GetItemLinkHash(Item, hash);
	if (int len = strlen(hash)) {
		if (Clickable) {
			sprintf_s(Buffer, BufferSize, "%c0%s%s%c", 0x12, hash, GetItemFromContents(Item)->Name, 0x12);
		}
		else {
			sprintf_s(Buffer, BufferSize, "0%s%s", hash, GetItemFromContents(Item)->Name);
		}
		retVal = true;
	}
	return retVal;
}

const char* GetLoginName()
{
	if (__LoginName)
	{
		return (char*)__LoginName;
	}

	return nullptr;
}

void STMLToPlainText(char* in, char* out)
{
	DWORD pchar_in_string_position = 0;
	DWORD pchar_out_string_position = 0;
	DWORD pchar_amper_string_position = 0;
	char Amper[2048] = { 0 };
	while (in[pchar_in_string_position] != 0) {
		switch (in[pchar_in_string_position]) {
		case '<':
			while (in[pchar_in_string_position] != '>')
				pchar_in_string_position++;
			pchar_in_string_position++;
			break;
		case '&':
			pchar_in_string_position++;
			pchar_amper_string_position = 0;
			ZeroMemory(Amper, 2048);
			while (in[pchar_in_string_position] != ';') {
				Amper[pchar_amper_string_position++] = in[pchar_in_string_position++];
			}
			pchar_in_string_position++;
			if (!_stricmp(Amper, "nbsp")) {
				out[pchar_out_string_position++] = ' ';
			}
			else if (!_stricmp(Amper, "amp")) {
				out[pchar_out_string_position++] = '&';
			}
			else if (!_stricmp(Amper, "gt")) {
				out[pchar_out_string_position++] = '>';
			}
			else if (!_stricmp(Amper, "lt")) {
				out[pchar_out_string_position++] = '<';
			}
			else if (!_stricmp(Amper, "quot")) {
				out[pchar_out_string_position++] = '\"';
			}
			else if (!_stricmp(Amper, "pct")) {
				out[pchar_out_string_position++] = '%';
			}
			else {
				out[pchar_out_string_position++] = '?';
			}
			break;
		default:
			out[pchar_out_string_position++] = in[pchar_in_string_position++];
		}
	}
	out[pchar_out_string_position++] = 0;
}

void ClearSearchItem(MQItemSearch& SearchItem)
{
	SearchItem = MQItemSearch();
}

#define MaskSet(n) (SearchItem.FlagMask[(SearchItemFlag)n])
#define Flag(n) (SearchItem.Flag[(SearchItemFlag)n])
#define RequireFlag(flag,value) {if (MaskSet(flag) && Flag(flag)!=(char)((value)!=0)) return false;}

bool ItemMatchesSearch(MQItemSearch& SearchItem, CONTENTS* pContents)
{
	if (SearchItem.ID && GetItemFromContents(pContents)->ItemNumber != SearchItem.ID)
		return false;

	RequireFlag(Lore, GetItemFromContents(pContents)->Lore);
	RequireFlag(NoRent, GetItemFromContents(pContents)->NoRent);
	RequireFlag(NoDrop, GetItemFromContents(pContents)->NoDrop);
	RequireFlag(Magic, GetItemFromContents(pContents)->Magic);
	RequireFlag(Pack, GetItemFromContents(pContents)->Type == ITEMTYPE_PACK);
	RequireFlag(Book, GetItemFromContents(pContents)->Type == ITEMTYPE_BOOK);
	RequireFlag(Combinable, GetItemFromContents(pContents)->ItemType == 17);
	RequireFlag(Summoned, GetItemFromContents(pContents)->Summoned);
	RequireFlag(Instrument, GetItemFromContents(pContents)->InstrumentType);
	RequireFlag(Weapon, GetItemFromContents(pContents)->Damage && GetItemFromContents(pContents)->Delay);
	RequireFlag(Normal, GetItemFromContents(pContents)->Type == ITEMTYPE_NORMAL);

	char szName[ITEM_NAME_LEN] = { 0 };
	strcpy_s(szName, GetItemFromContents(pContents)->Name);
	_strlwr_s(szName);

	if (SearchItem.szName[0] && !strstr(szName, SearchItem.szName))
		return false;

	return true;
}

bool SearchThroughItems(MQItemSearch& SearchItem, CONTENTS** pResult, DWORD* nResult)
{
	// TODO
#define DoResult(pContents, nresult) { \
	if (pResult)                       \
		*pResult = pContents;          \
	if (nResult)                       \
		*nResult = nresult;            \
	return true;                       \
}

	if (CHARINFO2 * pChar2 = GetCharInfo2())
	{
		if (pChar2->pInventoryArray)
		{
			if (MaskSet(Worn) && Flag(Worn))
			{
				// iterate through worn items
				for (unsigned long N = 0; N < 21; N++)
				{
					if (CONTENTS * pContents = pChar2->pInventoryArray->InventoryArray[N]) {
						if (ItemMatchesSearch(SearchItem, pContents)) {
							DoResult(pContents, N);
						}
					}
				}
			}

			if (MaskSet(Inventory) && Flag(Inventory))
			{
				unsigned long nPack;
				// iterate through inventory slots before in-pack slots
				for (nPack = 0; nPack < 10; nPack++)
				{
					if (CONTENTS * pContents = pChar2->pInventoryArray->Inventory.Pack[nPack])
					{
						if (ItemMatchesSearch(SearchItem, pContents))
							DoResult(pContents, nPack + 21);
					}
				}
				for (nPack = 0; nPack < 10; nPack++)
				{
					if (CONTENTS * pContents = pChar2->pInventoryArray->Inventory.Pack[nPack])
					{
						if (GetItemFromContents(pContents)->Type == ITEMTYPE_PACK && pContents->Contents.ContainedItems.Capacity)
						{
							for (unsigned long nItem = 0; nItem < GetItemFromContents(pContents)->Slots; nItem++)
							{
								if (CONTENTS * pItem = pContents->GetContent(nItem))
									if (ItemMatchesSearch(SearchItem, pItem))
										DoResult(pItem, nPack * 100 + nItem);
							}
						}
					}
				}
			}
		}
	}

	// TODO
	return false;
}
#undef DoResult
#undef RequireFlag
#undef Flag
#undef MaskSet

void ClearSearchSpawn(MQSpawnSearch* pSearchSpawn)
{
	if (!pSearchSpawn) return;

	*pSearchSpawn = MQSpawnSearch();

	if (pCharSpawn)
		pSearchSpawn->zLoc = ((PSPAWNINFO)pCharSpawn)->Z;
	else if (pLocalPlayer)
		pSearchSpawn->zLoc = ((PSPAWNINFO)pLocalPlayer)->Z;
}

// *************************************************************************** 
// Function:    DistanceToPoint 
// Description: Return the distance between a spawn and the specified point 
// *************************************************************************** 
float DistanceToPoint(PSPAWNINFO pSpawn, float xLoc, float yLoc)
{
	float X = pSpawn->X - xLoc;
	float Y = pSpawn->Y - yLoc;
	return sqrtf(X * X + Y * Y);
}
// *************************************************************************** 
// Function:    Distance3DToPoint 
// Description: Return the distance between a spawn and the specified point 
// *************************************************************************** 
float Distance3DToPoint(PSPAWNINFO pSpawn, float xLoc, float yLoc, float zLoc)
{
	float dX = pSpawn->X - xLoc;
	float dY = pSpawn->Y - yLoc;
	float dZ = pSpawn->Z - zLoc;
	return sqrtf(dX * dX + dY * dY + dZ * dZ);
}
// *************************************************************************** 
// Function:    IsBardSong
// Description: Return TRUE if the spell is a bard song
// *************************************************************************** 
BOOL IsBardSong(PSPELL pSpell)
{
	return (((EQ_Spell*)pSpell)->GetSpellLevelNeeded(Bard) <= MAX_PC_LEVEL)
		&& !(pSpell->DurationWindow);
}

// *************************************************************************** 
// Function:    IsSPAEffect
// Description: Return TRUE if the spell contains the SPAEffect
// *************************************************************************** 
BOOL IsSPAEffect(PSPELL pSpell, LONG EffectID)
{
	for (int slot = 0; slot < GetSpellNumEffects(pSpell); slot++)
		if (GetSpellAttrib(pSpell, slot) == EffectID)
			return true;
	return false;
}

// *************************************************************************** 
// Function:    GetClassesFromMask
// Description: Return a comma delimited list of player short class names
//              If ALL classes are in the mask it will return "ALL",
//              if 4 or less are missing it will return "ALL EXCEPT: " and the
//              comma delimited list of play short class names that are excluded
// *************************************************************************** 
template <unsigned int _Size>
char* GetClassesFromMask(LONG mask, char(&szBuffer)[_Size])
{
	//WriteChatf("GetClassesFromMask:: MASK:%d", mask);
	int matching = 0;
	int excluding = 0;
	int numofclasses = Berserker;
	for (int playerclass = Warrior; playerclass <= Berserker; playerclass++) {
		if (mask & (1 << playerclass)) {
			matching++;
		}
		else {
			excluding++;
		}
	}
	if (matching == numofclasses) {
		strcat_s(szBuffer, "ALL");
	}
	else if (excluding <= 4) {
		strcat_s(szBuffer, "ALL EXCEPT: ");
		for (int playerclass = Warrior; playerclass <= Berserker; playerclass++) {
			if (!(mask & (1 << playerclass))) {
				if (strlen(szBuffer) > 12)
					strcat_s(szBuffer, ",");
				strcat_s(szBuffer, ClassInfo[playerclass].UCShortName);
			}
		}
	}
	else {
		for (int playerclass = Warrior; playerclass <= Berserker; playerclass++) {
			//WriteChatf("Checking playerclass(%d)", 1 << playerclass);
			if (mask & (1 << playerclass)) {
				if (strlen(szBuffer) > 0)
					strcat_s(szBuffer, ",");
				strcat_s(szBuffer, ClassInfo[playerclass].UCShortName);
			}
		}
	}
	return szBuffer;
}

// *************************************************************************** 
// Function:    GetSpellRestrictions 
// Description: Return the restrictions for the spell slot
// *************************************************************************** 
char* GetSpellRestrictions(PSPELL pSpell, unsigned int nIndex, char* szBuffer, size_t BufferSize)
{
	char szTemp[MAX_STRING] = { 0 };
	if (!szBuffer)
		return NULL;
	if (!pSpell) {
		szBuffer[0] = '\0';
		return(szBuffer);
	}
	//switch (pSpell->Base2[nIndex])
	switch (GetSpellBase2(pSpell, nIndex))
	{
	case 0:	strcat_s(szBuffer, BufferSize, "None"); break;
	case 100: strcat_s(szBuffer, BufferSize, "Only works on Animal or Humanoid"); break;
	case 101: strcat_s(szBuffer, BufferSize, "Only works on Dragon"); break;
	case 102: strcat_s(szBuffer, BufferSize, "Only works on Animal or Insect"); break;
	case 104: strcat_s(szBuffer, BufferSize, "Only works on Animal"); break;
	case 105: strcat_s(szBuffer, BufferSize, "Only works on Plant"); break;
	case 106: strcat_s(szBuffer, BufferSize, "Only works on Giant"); break;
	case 108: strcat_s(szBuffer, BufferSize, "Doesn't work on Animals or Humanoids"); break;
	case 109: strcat_s(szBuffer, BufferSize, "Only works on Bixie"); break;
	case 110: strcat_s(szBuffer, BufferSize, "Only works on Harpy"); break;
	case 111: strcat_s(szBuffer, BufferSize, "Only works on Gnoll"); break;
	case 112: strcat_s(szBuffer, BufferSize, "Only works on Sporali"); break;
	case 113: strcat_s(szBuffer, BufferSize, "Only works on Kobold"); break;
	case 114: strcat_s(szBuffer, BufferSize, "Only works on Shade"); break;
	case 115: strcat_s(szBuffer, BufferSize, "Only works on Drakkin"); break;
	case 117: strcat_s(szBuffer, BufferSize, "Only works on Animals or Plants"); break;
	case 118: strcat_s(szBuffer, BufferSize, "Only works on Summoned"); break;
	case 119: strcat_s(szBuffer, BufferSize, "Only works on Fire_Pet"); break;
	case 120: strcat_s(szBuffer, BufferSize, "Only works on Undead"); break;
	case 121: strcat_s(szBuffer, BufferSize, "Only works on Living"); break;
	case 122: strcat_s(szBuffer, BufferSize, "Only works on Fairy"); break;
	case 123: strcat_s(szBuffer, BufferSize, "Only works on Humanoid"); break;
	case 124: strcat_s(szBuffer, BufferSize, "Undead HP Less Than 10%"); break;
	case 125: strcat_s(szBuffer, BufferSize, "Clockwork HP Less Than 45%"); break;
	case 126: strcat_s(szBuffer, BufferSize, "Wisp HP Less Than 10%"); break;
	case 190: strcat_s(szBuffer, BufferSize, "Doesn't work on Raid Bosses"); break;
	case 191: strcat_s(szBuffer, BufferSize, "Only works on Raid Bosses"); break;
	case 201: strcat_s(szBuffer, BufferSize, "HP Above 75%"); break;
	case 203: strcat_s(szBuffer, BufferSize, "HP Less Than 20%"); break;
	case 204: strcat_s(szBuffer, BufferSize, "HP Less Than 50%"); break;
	case 216: strcat_s(szBuffer, BufferSize, "Not In Combat"); break;
	case 221: strcat_s(szBuffer, BufferSize, "At Least 1 Pet On Hatelist"); break;
	case 222: strcat_s(szBuffer, BufferSize, "At Least 2 Pets On Hatelist"); break;
	case 223: strcat_s(szBuffer, BufferSize, "At Least 3 Pets On Hatelist"); break;
	case 224: strcat_s(szBuffer, BufferSize, "At Least 4 Pets On Hatelist"); break;
	case 225: strcat_s(szBuffer, BufferSize, "At Least 5 Pets On Hatelist"); break;
	case 226: strcat_s(szBuffer, BufferSize, "At Least 6 Pets On Hatelist"); break;
	case 227: strcat_s(szBuffer, BufferSize, "At Least 7 Pets On Hatelist"); break;
	case 228: strcat_s(szBuffer, BufferSize, "At Least 8 Pets On Hatelist"); break;
	case 229: strcat_s(szBuffer, BufferSize, "At Least 9 Pets On Hatelist"); break;
	case 230: strcat_s(szBuffer, BufferSize, "At Least 10 Pets On Hatelist"); break;
	case 231: strcat_s(szBuffer, BufferSize, "At Least 11 Pets On Hatelist"); break;
	case 232: strcat_s(szBuffer, BufferSize, "At Least 12 Pets On Hatelist"); break;
	case 233: strcat_s(szBuffer, BufferSize, "At Least 13 Pets On Hatelist"); break;
	case 234: strcat_s(szBuffer, BufferSize, "At Least 14 Pets On Hatelist"); break;
	case 235: strcat_s(szBuffer, BufferSize, "At Least 15 Pets On Hatelist"); break;
	case 236: strcat_s(szBuffer, BufferSize, "At Least 16 Pets On Hatelist"); break;
	case 237: strcat_s(szBuffer, BufferSize, "At Least 17 Pets On Hatelist"); break;
	case 238: strcat_s(szBuffer, BufferSize, "At Least 18 Pets On Hatelist"); break;
	case 239: strcat_s(szBuffer, BufferSize, "At Least 19 Pets On Hatelist"); break;
	case 240: strcat_s(szBuffer, BufferSize, "At Least 20 Pets On Hatelist"); break;
	case 250: strcat_s(szBuffer, BufferSize, "HP Less Than 35%"); break;
	case 304: strcat_s(szBuffer, BufferSize, "Chain Plate Classes"); break;
	case 399: strcat_s(szBuffer, BufferSize, "HP Between 15 and 25%"); break;
	case 400: strcat_s(szBuffer, BufferSize, "HP Between 1 and 25%"); break;
	case 401: strcat_s(szBuffer, BufferSize, "HP Between 25 and 35%"); break;
	case 402: strcat_s(szBuffer, BufferSize, "HP Between 35 and 45%"); break;
	case 403: strcat_s(szBuffer, BufferSize, "HP Between 45 and 55%"); break;
	case 404: strcat_s(szBuffer, BufferSize, "HP Between 55 and 65%"); break;
	case 412: strcat_s(szBuffer, BufferSize, "HP Above 99%"); break;
	case 501: strcat_s(szBuffer, BufferSize, "HP Below 5%"); break;
	case 502: strcat_s(szBuffer, BufferSize, "HP Below 10%"); break;
	case 503: strcat_s(szBuffer, BufferSize, "HP Below 15%"); break;
	case 504: strcat_s(szBuffer, BufferSize, "HP Below 20%"); break;
	case 505: strcat_s(szBuffer, BufferSize, "HP Below 25%"); break;
	case 506: strcat_s(szBuffer, BufferSize, "HP Below 30%"); break;
	case 507: strcat_s(szBuffer, BufferSize, "HP Below 35%"); break;
	case 508: strcat_s(szBuffer, BufferSize, "HP Below 40%"); break;
	case 509: strcat_s(szBuffer, BufferSize, "HP Below 45%"); break;
	case 510: strcat_s(szBuffer, BufferSize, "HP Below 50%"); break;
	case 511: strcat_s(szBuffer, BufferSize, "HP Below 55%"); break;
	case 512: strcat_s(szBuffer, BufferSize, "HP Below 60%"); break;
	case 513: strcat_s(szBuffer, BufferSize, "HP Below 65%"); break;
	case 514: strcat_s(szBuffer, BufferSize, "HP Below 70%"); break;
	case 515: strcat_s(szBuffer, BufferSize, "HP Below 75%"); break;
	case 516: strcat_s(szBuffer, BufferSize, "HP Below 80%"); break;
	case 517: strcat_s(szBuffer, BufferSize, "HP Below 85%"); break;
	case 518: strcat_s(szBuffer, BufferSize, "HP Below 90%"); break;
	case 519: strcat_s(szBuffer, BufferSize, "HP Below 95%"); break;
	case 521: strcat_s(szBuffer, BufferSize, "Mana Below X%"); break;
	case 522: strcat_s(szBuffer, BufferSize, "End Below 40%"); break;
	case 523: strcat_s(szBuffer, BufferSize, "Mana Below 40%"); break;
	case 603: strcat_s(szBuffer, BufferSize, "Only works on Undead2"); break;
	case 608: strcat_s(szBuffer, BufferSize, "Only works on Undead3"); break;
	case 624: strcat_s(szBuffer, BufferSize, "Only works on Summoned2"); break;
	case 701: strcat_s(szBuffer, BufferSize, "Doesn't work on Pets"); break;
	case 818: strcat_s(szBuffer, BufferSize, "Only works on Undead4"); break;
	case 819: strcat_s(szBuffer, BufferSize, "Doesn't work on Undead4"); break;
	case 825: strcat_s(szBuffer, BufferSize, "End Below 21%"); break;
	case 826: strcat_s(szBuffer, BufferSize, "End Below 25%"); break;
	case 827: strcat_s(szBuffer, BufferSize, "End Below 29%"); break;
	case 836: strcat_s(szBuffer, BufferSize, "Only works on Regular Servers"); break;
	case 837: strcat_s(szBuffer, BufferSize, "Doesn't work on Progression Servers"); break;
	case 842: strcat_s(szBuffer, BufferSize, "Only works on Humanoid Level 84 Max"); break;
	case 843: strcat_s(szBuffer, BufferSize, "Only works on Humanoid Level 86 Max"); break;
	case 844: strcat_s(szBuffer, BufferSize, "Only works on Humanoid Level 88 Max"); break;
	case 1000: strcat_s(szBuffer, BufferSize, "Between Level 1 and 75"); break;
	case 1001: strcat_s(szBuffer, BufferSize, "Between Level 76 and 85"); break;
	case 1002: strcat_s(szBuffer, BufferSize, "Between Level 86 and 95"); break;
	case 1003: strcat_s(szBuffer, BufferSize, "Between Level 96 and 100"); break;
	case 1004: strcat_s(szBuffer, BufferSize, "HP Less Than 80%"); break;
	case 38311: strcat_s(szBuffer, BufferSize, "Mana Below 20%"); break;
	case 38312: strcat_s(szBuffer, BufferSize, "Mana Below 10%"); break;
	default:
		sprintf_s(szTemp, "Unknown[%d]", GetSpellBase2(pSpell, nIndex));
		strcat_s(szBuffer, BufferSize, szTemp); break;
	}
	return szBuffer;
}

// ***************************************************************************
// Function:    GetSpellEffectName, GetSpellEffectNameByID
// Description: Return spell effect string 
// ***************************************************************************
char* GetSpellEffectNameByID(LONG EffectID, char* szBuffer, size_t BufferSize)
{
	return GetSpellEffectName(abs(EffectID), szBuffer, BufferSize);
}

char* GetSpellEffectName(LONG EffectID, char* szBuffer, size_t BufferSize)
{
	//we CAN do an abs here cause IF it is negative, it just means we should display is as "Exclude: "
	ULONG absEffectID = abs(EffectID);
	if ((size_t)absEffectID < MAX_SPELLEFFECTS) {
		strcat_s(szBuffer, BufferSize, szSPATypes[absEffectID]);
	}
	else {
		char szTemp[MAX_STRING] = { 0 };
		sprintf_s(szTemp, "Unknown SPA[%03d]", absEffectID);
		strcat_s(szBuffer, BufferSize, szTemp);
	}
	return szBuffer;
}

template <unsigned int _Size>
char* GetResistTypeName(LONG ResistType, char(&szBuffer)[_Size])
{
	char szTemp[MAX_STRING] = { 0 };
	switch (ResistType)
	{
	case 1: strcat_s(szBuffer, "Magic"); break;
	case 2: strcat_s(szBuffer, "Fire"); break;
	case 3: strcat_s(szBuffer, "Cold/Ice"); break;
	case 4: strcat_s(szBuffer, "Poison"); break;
	case 5: strcat_s(szBuffer, "Disease"); break;
	case 6: strcat_s(szBuffer, "Chromatic"); break;
	case 7: strcat_s(szBuffer, "Prismatic"); break;
	default:
		sprintf_s(szTemp, "Unknown[%d]", ResistType);
		strcat_s(szBuffer, szTemp); break;
	}
	return szBuffer;
}

template <unsigned int _Size>
char* GetSpellTypeName(LONG SpellType, char(&szBuffer)[_Size])
{
	char szTemp[MAX_STRING] = { 0 };
	switch (SpellType)
	{
	case 0: strcat_s(szBuffer, "Detrimental only"); break;
	case 1: strcat_s(szBuffer, "Beneficial only"); break;
	case 2: strcat_s(szBuffer, "Beneficial - Group Only"); break;
	default:
		sprintf_s(szTemp, "Unknown[%d]", SpellType);
		strcat_s(szBuffer, szTemp); break;
	}
	return szBuffer;
}

template <unsigned int _Size>
char* GetTargetTypeLimitsName(LONG TargetLimitsType, char(&szBuffer)[_Size])
{
	char szTemp[MAX_STRING] = { 0 };
	switch (abs(TargetLimitsType))
	{
	case 50: strcat_s(szBuffer, "Target AE No Players Pets"); break; // blanket of forgetfullness. beneficial, AE mem blur, with max targets
	case 47: strcat_s(szBuffer, "Pet Owner"); break;
	case 46: strcat_s(szBuffer, "Target of Target"); break;
	case 45: strcat_s(szBuffer, "Free Target"); break;
	case 44: strcat_s(szBuffer, "Beam"); break;
	case 43: strcat_s(szBuffer, "Single in Group"); break;
	case 42: strcat_s(szBuffer, "Directional AE"); break;
	case 39: strcat_s(szBuffer, "No Pets"); break;
	case 38: strcat_s(szBuffer, "Pet2"); break;
	case 37: strcat_s(szBuffer, "Caster PB NPC"); break;
	case 36: strcat_s(szBuffer, "Caster PB PC"); break;
	case 35: strcat_s(szBuffer, "Special Muramites"); break;
	case 34: strcat_s(szBuffer, "Chest"); break;
	case 33: strcat_s(szBuffer, "Hatelist2"); break;
	case 32: strcat_s(szBuffer, "Hatelist"); break;
	case 41: strcat_s(szBuffer, "Group v2"); break;
	case 40: strcat_s(szBuffer, "AE PC v2"); break;
	case 25: strcat_s(szBuffer, "AE Summoned"); break;
	case 24: strcat_s(szBuffer, "AE Undead"); break;
	case 20: strcat_s(szBuffer, "Targeted AE Tap"); break;
	case 18: strcat_s(szBuffer, "Uber Dragons"); break;
	case 17: strcat_s(szBuffer, "Uber Giants"); break;
	case 16: strcat_s(szBuffer, "Plant"); break;
	case 15: strcat_s(szBuffer, "Corpse"); break;
	case 14: strcat_s(szBuffer, "Pet"); break;
	case 13: strcat_s(szBuffer, "LifeTap"); break;
	case 11: strcat_s(szBuffer, "Summoned"); break;
	case 10: strcat_s(szBuffer, "Undead"); break;
	case 9: strcat_s(szBuffer, "Animal"); break;
	case 8: strcat_s(szBuffer, "Targeted AE"); break;
	case 6: strcat_s(szBuffer, "Self"); break;
	case 5: strcat_s(szBuffer, "Single"); break;
	case 4: strcat_s(szBuffer, "PB AE"); break;
	case 3: strcat_s(szBuffer, "Group v1"); break;
	case 2: strcat_s(szBuffer, "AE PC v1"); break;
	case 1: strcat_s(szBuffer, "Line of Sight"); break;
	default:
		sprintf_s(szTemp, "Unknown[%d]", abs(TargetLimitsType));
		strcat_s(szBuffer, szTemp); break;
	}
	return szBuffer;
}

template <unsigned int _Size>
char* GetStatShortName(LONG StatType, char(&szBuffer)[_Size])
{
	char szTemp[MAX_STRING] = { 0 };
	switch (StatType)
	{
	case 0: strcat_s(szBuffer, "STR"); break;
	case 1: strcat_s(szBuffer, "STA"); break;
	case 2: strcat_s(szBuffer, "AGI"); break;
	case 3: strcat_s(szBuffer, "DEX"); break;
	case 4: strcat_s(szBuffer, "WIS"); break;
	case 5: strcat_s(szBuffer, "INT"); break;
	case 6: strcat_s(szBuffer, "CHA"); break;
	case 7: strcat_s(szBuffer, "MR"); break;
	case 8: strcat_s(szBuffer, "CR"); break;
	case 9: strcat_s(szBuffer, "FR"); break;
	case 10: strcat_s(szBuffer, "PR"); break;  // either PR or DR
	case 11: strcat_s(szBuffer, "DR"); break;  // either DR or PR
	default:
		sprintf_s(szTemp, "Unknown[%d]", StatType);
		strcat_s(szBuffer, szTemp); break;
	}
	return szBuffer;
}

template <unsigned int _Size>
char* GetFactionName(LONG FactionID, char(&szBuffer)[_Size])
{
	/*
	char szTemp[MAX_STRING] = { 0 };
	switch (FactionType)
	{
	case 304: strcat_s(szBuffer, "(Ring of Scale)"); break;
	case 306: strcat_s(szBuffer, "(Blackburrow Gnolls)"); break;
	case 430: strcat_s(szBuffer, "(Claws of Veeshan)"); break;
	case 1150: strcat_s(szBuffer, "(Jewel of Atiiki Efreetis)"); break;
	case 1178: strcat_s(szBuffer, "(S.H.I.P. Workshop Base Population)"); break;
	case 1229: strcat_s(szBuffer, "(Sebilisian Empire)"); break;
	default:
		sprintf_s(szTemp, "(Unknown[%d])", FactionType);
		strcat_s(szBuffer, szTemp); break;
	}
	*/
	if ((size_t)FactionID < MAX_FACTIONNAMES) {
		strcat_s(szBuffer, _Size, szFactionNames[FactionID]);
	}
	else {
		char szTemp[MAX_STRING] = { 0 };
		sprintf_s(szTemp, "Unknown Faction[%d]", FactionID);
		strcat_s(szBuffer, _Size, szTemp);
	}
	return szBuffer;
}

LONG CalcDuration(LONG calc, LONG max, LONG level)
{
	LONG value = 0;

	switch (calc)
	{
	case 0:  value = 0; break;
	case 1:
	case 12:
		value = level / 2;
		if (value < 1)
			value = 1;
		break;
	case 2:
		value = (level / 2) + 5;
		if (value < 6)
			value = 6;
		break;
	case 3:  value = level * 30; break;
	case 4:  value = 50; break;
	case 5:  value = 2; break;
	case 6:  value = level / 2; break;
	case 7:  value = level; break;
	case 8:  value = level + 10; break;
	case 9:  value = level * 2 + 10; break;
	case 10: value = level * 30 + 10; break;
	case 11: value = (level + 3) * 30; break;
	case 13: value = level * 3 + 10; break;
	case 14: value = (level + 2) * 5; break;
	case 15: value = (level + 10) * 10; break;
	case 50: value = 72000; break;
	case 3600: value = 3600; break;
	default: value = max;
	}

	if (max > 0 && value > max)
		value = max;

	return value;
}

LONG CalcValue(LONG calc, LONG base, LONG max, LONG tick, LONG minlevel, LONG level)
{
	if (calc == 0)
		return base;
	if (calc == 100) {
		if (max > 0 && ((base > max) || (level > minlevel)))
			return max;
		return base;
	}

	LONG change = 0;
	LONG adjustment = 0;

	switch (calc)
	{
	case 100:
		break;
	case 101:
		change = level / 2;
		break;
	case 102:
		change = level;
		break;
	case 103:
		change = level * 2;
		break;
	case 104:
		change = level * 3;
		break;
	case 105:
		change = level * 4;
		break;
	case 106:
		change = level * 5;
		break;
	case 107:
		change = -1 * tick;
		break;
	case 108:
		change = -2 * tick;
		break;
	case 109:
		change = level / 4;
		break;
	case 110:
		change = level / 6;
		break;
	case 111:
		//if (level < 16) adjustment = (level - 16) * 6;
		if (level > 16) change = (level - 16) * 6;
		break;
	case 112:
		//if (level < 24) adjustment = (level - 24) * 8;
		if (level > 24) change = (level - 24) * 8;
		break;
	case 113:
		//if (level < 34) adjustment = (level - 34) * 10;
		if (level > 34) change = (level - 34) * 10;
		break;
	case 114:
		//if (level < 44) adjustment = (level - 44) * 15;
		if (level > 44) change = (level - 44) * 15;
		break;
	case 115:
		//if (level < 15) adjustment = (level - 15) * 7;
		if (level > 15) change = (level - 15) * 7;
		break;
	case 116:
		//if (level < 24) adjustment = (level - 24) * 10;
		if (level > 24) change = (level - 24) * 10;
		break;
	case 117:
		//if (level < 34) adjustment = (level - 34) * 13;
		if (level > 34) change = (level - 34) * 13;
		break;
	case 118:
		//if (level < 44) adjustment = (level - 44) * 20;
		if (level > 44) change = (level - 44) * 20;
		break;
	case 119:
		change = level / 8;
		break;
	case 120:
		change = -5 * tick;
		break;
	case 121:
		change = level / 3;
		break;
	case 122:
		change = -12 * tick;
		break;
	case 123: // random in range
		if (tick > 1) change = abs(max) - abs(base);
		//change = (abs(max) - abs(base)) / 2;
		break;
	case 124:
		//if (level < 50) adjustment = (level - 50);
		if (level > 50) change = (level - 50);
		break;
	case 125:
		//if (level < 50) adjustment = (level - 50) * 2;
		if (level > 50) change = (level - 50) * 2;
		break;
	case 126:
		//if (level < 50) adjustment = (level - 50) * 3;
		if (level > 50) change = (level - 50) * 3;
		break;
	case 127:
		//if (level < 50) adjustment = (level - 50) * 4;
		if (level > 50) change = (level - 50) * 4;
		break;
	case 128:
		//if (level < 50) adjustment = (level - 50) * 5;
		if (level > 50) change = (level - 50) * 5;
		break;
	case 129:
		//if (level < 50) adjustment = (level - 50) * 10;
		if (level > 50) change = (level - 50) * 10;
		break;
	case 130:
		//if (level < 50) adjustment = (level - 50) * 15;
		if (level > 50) change = (level - 50) * 15;
		break;
	case 131:
		//if (level < 50) adjustment = (level - 50) * 20;
		if (level > 50) change = (level - 50) * 20;
		break;
	case 132:
		//if (level < 50) adjustment = (level - 50) * 25;
		if (level > 50) change = (level - 50) * 25;
		break;
	case 139:
		//if (level < 30) adjustment = (level - 30) / 2;
		if (level > 30) change = (level - 30) / 2;
		break;
	case 140:
		//if (level < 30) adjustment = (level - 30);
		if (level > 30) change = (level - 30);
		break;
	case 141:
		//if (level < 30) adjustment = 3 * (level - 30) / 2;
		if (level > 30) change = 3 * (level - 30) / 2;
		break;
	case 142:
		//if (level < 30) adjustment = 2 * (level - 30);
		if (level > 30) change = 2 * (level - 30);
		break;
	case 143:
		change = 3 * level / 4;
		break;
	case 3000:
		return base;
	default:
		if (calc > 0 && calc < 1000)
			change = level * calc;
		if (calc >= 1000 && calc < 2000)
			change = tick * (calc - 1000) * -1;
		if (calc >= 2000)
			change = level * (calc - 2000);
	}

	LONG value = abs(base) + adjustment + change;
	//WriteChatf("#1-VALUE:%d", value);

	if (max != 0 && value > abs(max))
		value = abs(max);
	//WriteChatf("#2-VALUE:%d", value);

	if (base < 0)
		value = -value;
	//WriteChatf("#3-BASE: %d, VALUE:%d", base, value);

	return value;
}

LONG CalcMaxSpellLevel(LONG calc, LONG base, LONG max, LONG tick, LONG minlevel, LONG level)
{
	//WriteChatf("CalcMaxSpellLevel(CALC:%d, BASE:%d, MAX:%d, TICK:%d, LEVEL:%d)", calc, base, max, tick, level);
	if (abs(max) > 0) {
		//WriteChatf("Inside if (abs(max)>0)");
		for (LONG maxlevel = 1; maxlevel <= level; maxlevel++) {
			LONG value = CalcValue(calc, base, max, tick, minlevel, maxlevel);
			//WriteChatf("VALUE:%d, MAX:%d", abs(value), abs(max));
			if (abs(CalcValue(calc, base, max, tick, minlevel, maxlevel)) >= abs(max))
				return maxlevel;
		}
		return level;
	}
	return MAX_PC_LEVEL;
}

LONG CalcMinSpellLevel(PSPELL pSpell)
{
	LONG minspelllvl = ((EQ_Spell*)pSpell)->GetSpellLevelNeeded(Warrior);
	for (LONG j = Warrior; j <= Berserker; j++)
		if (((EQ_Spell*)pSpell)->GetSpellLevelNeeded(j) < minspelllvl)  minspelllvl = ((EQ_Spell*)pSpell)->GetSpellLevelNeeded(j);
	if (minspelllvl > MAX_PC_LEVEL)
		minspelllvl = 1;
	return minspelllvl;
}

char* CalcValueRange(LONG calc, LONG base, LONG max, LONG duration, LONG minlevel, LONG level, char* szBuffer, size_t BufferSize, char* szPercent)
{
	LONG start = CalcValue(calc, base, max, 1, minlevel, minlevel);
	LONG finish = CalcValue(calc, base, max, duration, minlevel, level);
	char type[MAX_STRING] = { 0 };

	sprintf_s(type, "%s", abs(start) < abs(finish) ? "Growing" : "Decaying");

	switch (calc)
	{
	case CALC_1TICK:
		sprintf_s(szBuffer, BufferSize, " (%s to %d @ 1/tick)", type, finish);
		break;
	case CALC_2TICK:
		sprintf_s(szBuffer, BufferSize, " (%s to %d @ 2/tick)", type, finish);
		break;
	case CALC_5TICK:
		sprintf_s(szBuffer, BufferSize, " (%s to %d @ 5/tick)", type, finish);
		break;
	case CALC_12TICK:
		sprintf_s(szBuffer, BufferSize, " (%s to %d @ 12/tick)", type, finish);
		break;
	case CALC_RANDOM:
		sprintf_s(szBuffer, BufferSize, " (Random: %d to %d)", start, finish * ((start >= 0) ? 1 : -1));
		break;
	default:
		if (calc > 0 && calc < 1000)
			sprintf_s(szBuffer, BufferSize, " to %d%s", start, szPercent);
		if (calc >= 1000 && calc < 2000)
			sprintf_s(szBuffer, BufferSize, " (%s to %d @ %d/tick)", type, finish, calc - 1000);
	}
	return szBuffer;
}

char* CalcExtendedRange(LONG calc, LONG start, LONG finish, LONG minlevel, LONG maxlevel, char* szBuffer, size_t BufferSize, char* szPercent, BOOL ACMod = FALSE)
{
	switch (calc)
	{
	case CALC_RANDOM:
		sprintf_s(szBuffer, BufferSize, " (Random: %d to %d)", start, finish * ((start >= 0) ? 1 : -1));
		break;
	default:
		if (abs(start) < abs(finish))
			sprintf_s(szBuffer, BufferSize, " by %d%s (L%d) to %d%s (L%d)", ACMod ? (LONG)(abs(start) / (10.0f / 3.0f)) : abs(start), szPercent, minlevel, ACMod ? (LONG)(abs(finish) / (10.0f / 3.0f)) : abs(finish), szPercent, maxlevel);
		else
			sprintf_s(szBuffer, BufferSize, " by %d%s", ACMod ? (LONG)(abs(finish) / (10.0f / 3.0f)) : abs(finish), szPercent);
	}
	return szBuffer;
}

template <unsigned int _Size> char* FormatAT(char* szEffectName, LONG value, char(&szBuffer)[_Size], char* preposition = "by", char* szPercent = "")
{
	sprintf_s(szBuffer, "%s %s %d%s", szEffectName, preposition, abs(value), szPercent);
	return szBuffer;
}

template <unsigned int _Size> char* FormatBase(char* szEffectName, LONG base, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s (%d)", szEffectName, base);
	return szBuffer;
}

template <unsigned int _Size> char* FormatBase(char* szEffectName, LONG base, LONG max, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s (%d,%d)", szEffectName, base, max);
	return szBuffer;
}

template <unsigned int _Size> char* FormatBase(char* szEffectName, LONG base, char* szOptional, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s %s (%d)", szEffectName, szOptional, base);
	return szBuffer;
}

template <unsigned int _Size> char* FormatBasePercent(char* szEffectName, LONG base, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s (%d%%)", szEffectName, base);
	return szBuffer;
}

template <unsigned int _Size> char* FormatMinMaxBase(char* szEffectName, LONG base, LONG spa, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s (%d %s)", szEffectName, abs(base), szSPATypes[spa]);
	return szBuffer;
}

template <unsigned int _Size> char* FormatCount(char* szEffectName, LONG value, char(&szBuffer)[_Size], char* preposition = "by", char* szPercent = "")
{
	sprintf_s(szBuffer, "%s %s %s %d%s", value < 0 ? "Decrease" : "Increase", szEffectName, preposition, abs(value), szPercent);
	return szBuffer;
}

template <unsigned int _Size>
char* FormatExtra(const char* szEffectName, const char* extra, char(&szBuffer)[_Size], const char* trigger = "", const char* colon = ":")
{
	sprintf_s(szBuffer, "%s%s %s%s", szEffectName, colon, extra, trigger);
	return szBuffer;
}

template <unsigned int _Size>
char* FormatLimits(char* szEffectName, LONG value, char* extra, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s (%s %s)", szEffectName, extra, value < 0 ? "excluded" : "allowed");
	return szBuffer;
}

template <unsigned int _Size> char* FormatMax(char* szEffectName, LONG value, LONG max, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s %s by %d (%d%% max)", max < 0 ? "Decrease" : "Increase", szEffectName, abs(max), value);
	return szBuffer;
}

template <unsigned int _Size> char* FormatPenaltyChance(char* szEffectName, LONG value, char(&szBuffer)[_Size], char* szPercent, char* penaltychance)
{
	if (value < 100)
		sprintf_s(szBuffer, "%s (%d%s %s)", szEffectName, value, szPercent, penaltychance);
	else
		sprintf_s(szBuffer, "%s", szEffectName);
	return szBuffer;
}

template <unsigned int _Size> char* FormatPercent(char* szEffectName, LONG value, LONG max, char(&szBuffer)[_Size], BOOL scaling = TRUE, BOOL hundreds = FALSE, BOOL usepercent = TRUE)
{
	char szPercent[MAX_STRING] = { 0 };
	if (usepercent) strcat_s(szPercent, "%");
	if (hundreds)
		if (value == max)
			if (scaling)
				sprintf_s(szBuffer, "%s %s by %.2f%s", max < 0 ? "Decrease" : "Increase", szEffectName, abs(max / 100.0f), szPercent);
			else
				sprintf_s(szBuffer, "%s by %.2f%s", szEffectName, abs(max / 100.0f), szPercent);
		else
			if (scaling)
				sprintf_s(szBuffer, "%s %s by %.2f%s to %.2f%s", max < 0 ? "Decrease" : "Increase", szEffectName, abs(value / 100.0f), szPercent, abs(max / 100.0f), szPercent);
			else
				sprintf_s(szBuffer, "%s by %.2f%s to %.2f%s", szEffectName, abs(value / 100.0f), szPercent, abs(max / 100.0f), szPercent);
	else
		if (value == max)
			if (scaling)
				sprintf_s(szBuffer, "%s %s by %d%s", max < 0 ? "Decrease" : "Increase", szEffectName, abs(max), szPercent);
			else
				sprintf_s(szBuffer, "%s by %d%s", szEffectName, abs(max), szPercent);
		else
			if (scaling)
				sprintf_s(szBuffer, "%s %s by %d%s to %d%s", max < 0 ? "Decrease" : "Increase", szEffectName, abs(value), szPercent, abs(max), szPercent);
			else
				sprintf_s(szBuffer, "%s by %d%s to %d%s", szEffectName, abs(value), szPercent, abs(max), szPercent);
	return szBuffer;
}

template <unsigned int _Size> char* FormatPercent(char* szEffectName, LONG value, char(&szBuffer)[_Size], BOOL scaling = TRUE, BOOL hundreds = FALSE, BOOL usepercent = TRUE)
{
	return FormatPercent(szEffectName, value, value, szBuffer, scaling, hundreds, usepercent);
}

template <unsigned int _Size> char* FormatRange(char* szEffectName, LONG value, char* range, char(&szBuffer)[_Size], char* extra = "")
{
	sprintf_s(szBuffer, "%s %s%s%s", value < 0 ? "Decrease" : "Increase", szEffectName, range, extra);
	return szBuffer;
}

template <unsigned int _Size> char* FormatRateMod(char* szEffectName, LONG value, LONG base, char(&szBuffer)[_Size])
{
	if (base > 0)
		sprintf_s(szBuffer, "%s (rate mod %d)", GetSpellNameByID(value), base);
	else
		strcat_s(szBuffer, GetSpellNameByID(value));
	return szBuffer;
}

template <unsigned int _Size> char* FormatRefreshTimer(char* szEffectName, LONG value, LONG max, LONG skill, char(&szBuffer)[_Size], char* preposition = "with")
{
	if (value == max)
		sprintf_s(szBuffer, "%s %s by %d sec %s %s", max < 0 ? "Decrease" : "Increase", szEffectName, abs(max), preposition, skill >= 0 ? szSkills[skill] : "All Skills");
	else
		sprintf_s(szBuffer, "%s %s by %d sec to %d sec %s %s", max < 0 ? "Decrease" : "Increase", szEffectName, abs(value), abs(max), preposition, skill >= 0 ? szSkills[skill] : "All Skills");
	return szBuffer;
}

template <unsigned int _Size> char* FormatRefreshTimer(char* szEffectName, LONG value, LONG skill, char(&szBuffer)[_Size], char* preposition = "with")
{
	return FormatRefreshTimer(szEffectName, value, value, skill, szBuffer, preposition);
}

template <unsigned int _Size> char* FormatResists(char* szEffectName, LONG value, LONG base, char(&szBuffer)[_Size])
{
	if (value < 100) {
		char szTemp[MAX_STRING] = { 0 };
		sprintf_s(szBuffer, "%s (%d%% Chance)", GetSpellEffectNameByID(base, szTemp, MAX_STRING), value);
	}
	else {
		sprintf_s(szBuffer, "%s", szEffectName);
	}
	return szBuffer;
}

template <unsigned int _Size> char* FormatSeconds(char* szEffectName, LONG value, char(&szBuffer)[_Size], BOOL tens = FALSE)
{
	if (tens)
		sprintf_s(szBuffer, "%s (%d0.00 sec)", szEffectName, value);
	else
		sprintf_s(szBuffer, "%s (%d sec)", szEffectName, value);
	return szBuffer;
}

template <unsigned int _Size> char* FormatSeconds(char* szEffectName, float value, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s (%.2f sec)", szEffectName, value);
	return szBuffer;
}

template <unsigned int _Size> char* FormatSecondsCount(char* szEffectName, float value, char(&szBuffer)[_Size], char* preposition = "by")
{
	sprintf_s(szBuffer, "%s %s %s %.2f sec", value < 0 ? "Decrease" : "Increase", szEffectName, preposition, abs(value));
	return szBuffer;
}

template <unsigned int _Size> char* FormatSkillAttack(char* szEffectName, LONG value, LONG max, LONG base2, LONG skill, char(&szBuffer)[_Size], char* preposition = "with")
{
	sprintf_s(szBuffer, "%s %s %s for %d damage", FormatPercent(szEffectName, value, max, szBuffer), preposition, skill >= 0 ? szSkills[skill] : "All Skills", base2);
	return szBuffer;
}

template <unsigned int _Size> char* FormatSkillAttack(char* szEffectName, LONG value, LONG base2, LONG skill, char(&szBuffer)[_Size], char* preposition = "with")
{
	return FormatSkillAttack(szEffectName, base2, base2, value, skill, szBuffer, preposition);
}

template <unsigned int _Size> char* FormatSkills(char* szEffectName, LONG value, LONG max, LONG skill, char(&szBuffer)[_Size], BOOL usepercent = TRUE, char* preposition = "with")
{
	sprintf_s(szBuffer, "%s %s %s", FormatPercent(szEffectName, value, max, szBuffer, TRUE, FALSE, usepercent), preposition, skill >= 0 ? szSkills[skill] : "All Skills");
	return szBuffer;
}

template <unsigned int _Size> char* FormatSkills(char* szEffectName, LONG value, LONG skill, char(&szBuffer)[_Size], BOOL percent = TRUE, char* preposition = "with")
{
	return FormatSkills(szEffectName, value, value, skill, szBuffer, usepercent, preposition);
}

template <unsigned int _Size> char* FormatSpellChance(char* szEffectName, LONG value, LONG base, char(&szBuffer)[_Size])
{
	if (value < 100)
		sprintf_s(szBuffer, " (%d%% Chance, Spell: %s)", value, GetSpellNameByID(base));
	else
		sprintf_s(szBuffer, " (Spell: %s)", GetSpellNameByID(base));
	return szBuffer;
}

template <unsigned int _Size> char* FormatSpellGroupChance(char* szEffectName, LONG value, LONG base, char(&szBuffer)[_Size])
{
	if (value < 100)
		sprintf_s(szBuffer, " (%d%% Chance, Spell: %s)", value, GetSpellNameBySpellGroupID(base));
	else
		sprintf_s(szBuffer, " (Spell: %s)", GetSpellNameBySpellGroupID(base));
	return szBuffer;
}

template <unsigned int _Size> char* FormatStacking(char* szEffectName, LONG slot, LONG value, LONG max, LONG spa, char* extra, char(&szBuffer)[_Size])
{
	if (max > 0)
		sprintf_s(szBuffer, "%s %s spell if slot %d is effect '%s' and < %d", szEffectName, spa == 148 ? "new" : "existing", slot, extra, value);
	else
		sprintf_s(szBuffer, "%s %s spell if slot %d is effect '%s'", szEffectName, spa == 148 ? "new" : "existing", slot, extra);
	return szBuffer;
}

template <unsigned int _Size> char* FormatStatsCapRange(char* szEffectName, LONG value, char* stat, char* range, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s %s %s%s", value < 0 ? "Decrease" : "Increase", stat, szEffectName, range);
	return szBuffer;
}

template <unsigned int _Size> char* FormatString(char* szEffectName, char* extra, char(&szBuffer)[_Size], char* trigger = "")
//char* FormatString(char* szEffectName, char* extra, char(&szBuffer)[_Size], char* trigger = "")
{
	sprintf_s(szBuffer, "%s %s%s", szEffectName, extra, trigger);
	return szBuffer;
}

template <unsigned int _Size> char* FormatTimer(char* szEffectName, LONG value, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s by %d.00 sec", szEffectName, value);
	return szBuffer;
}

template <unsigned int _Size> char* FormatTimer(char* szEffectName, float value, char(&szBuffer)[_Size])
{
	sprintf_s(szBuffer, "%s by %.2f sec", szEffectName, value);
	return szBuffer;
}

int GetSpellAttrib(SPELL* pSpell, int index)
{
	if (index < 0)
		index = 0;

	if (pSpell && pSpellMgr)
	{
		int numeff = GetSpellNumEffects(pSpell);
		if (numeff == 0)
			return 0; // this is so stupid, it didnt use to do this prior to test on may 7 2018, what changed? we need to check that. -eqmule

		if (numeff > index)
		{
			if (PSPELLCALCINFO pCalcInfo = pSpellMgr->GetSpellAffect(pSpell->CalcIndex + index))
			{
				return pCalcInfo->Attrib;
			}
		}
		else
		{
			DebugSpewAlways("Bad usage of GetSpellAttrib: index=%d", index);
		}
	}
	return 0;
}

int GetSpellBase(SPELL* pSpell, int index)
{
	if (index < 0)
		index = 0;

	if (pSpell)
	{
		int numeff = GetSpellNumEffects(pSpell);
		if (numeff == 0)
			return 0;

		if (numeff > index)
		{
			if (ClientSpellManager * pSpellM = (ClientSpellManager*)pSpellMgr)
			{
				if (SPELLCALCINFO * pCalcInfo = pSpellM->GetSpellAffect(pSpell->CalcIndex + index))
				{
					return pCalcInfo->Base;
				}
			}
		}
	}

	return 0;
}

int GetSpellBase2(SPELL* pSpell, int index)
{
	if (index < 0)
		index = 0;

	if (pSpell)
	{
		int numeff = GetSpellNumEffects(pSpell);
		if (numeff == 0)
			return 0;

		if (numeff > index)
		{
			if (ClientSpellManager * pSpellM = (ClientSpellManager*)pSpellMgr)
			{
				if (SPELLCALCINFO * pCalcInfo = pSpellM->GetSpellAffect(pSpell->CalcIndex + index))
				{
					return pCalcInfo->Base2;
				}
			}
		}
	}

	return 0;
}

int GetSpellMax(SPELL* pSpell, int index)
{
	if (index < 0)
		index = 0;

	if (pSpell)
	{
		int numeff = GetSpellNumEffects(pSpell);
		if (numeff == 0)
			return 0;

		if (numeff > index)
		{
			if (ClientSpellManager * pSpellM = (ClientSpellManager*)pSpellMgr)
			{
				if (SPELLCALCINFO * pCalcInfo = pSpellM->GetSpellAffect(pSpell->CalcIndex + index))
				{
					return pCalcInfo->Max;
				}
			}
		}
	}

	return 0;
}

int GetSpellCalc(SPELL* pSpell, int index)
{
	if (index < 0)
		index = 0;

	if (pSpell)
	{
		int numeff = GetSpellNumEffects(pSpell);
		if (numeff == 0)
			return 0;

		if (numeff > index)
		{
			if (ClientSpellManager * pSpellM = (ClientSpellManager*)pSpellMgr)
			{
				if (SPELLCALCINFO * pCalcInfo = pSpellM->GetSpellAffect(pSpell->CalcIndex + index))
				{
					return pCalcInfo->Calc;
				}
			}
		}
	}

	return 0;
}

char* ParseSpellEffect(PSPELL pSpell, int i, char* szBuffer, size_t BufferSize, LONG level)
{
	char szBuff[MAX_STRING] = { 0 };
	char szTemp[MAX_STRING] = { 0 };
	char szTemp2[MAX_STRING] = { 0 };

	LONG id = pSpell->ID;
	LONG spa = GetSpellAttrib(pSpell, i);
	LONG base = GetSpellBase(pSpell, i);
	LONG base2 = GetSpellBase2(pSpell, i);
	LONG max = GetSpellMax(pSpell, i);
	LONG calc = GetSpellCalc(pSpell, i);
	LONG spellgroup = pSpell->SpellGroup;
	LONG ticks = pSpell->DurationCap;
	LONG targets = pSpell->MaxTargets;
	LONG targettype = pSpell->TargetType;
	LONG skill = pSpell->Skill;

	if (spa == SPA_NOSPELL)
		return szBuffer;

	if (spa == SPA_CHA && (base <= 1 || base > 255))
		return szBuffer;

	switch (spa)
	{
	case SPA_HASTE:
	case SPA_PLAYERSIZE:
	case SPA_BARDOVERHASTE: //Adjust for Base=100
		base -= 100;
		max -= 100;
		break;
	case SPA_SUMMONCORPSE: //Adjust for base/max swapped
		max = base;
		base = 0;
		break;
	case SPA_SPELLDAMAGE:
	case SPA_HEALING:
	case SPA_SPELLMANACOST: //Adjust for base2 used as max
		max = base2;
		break;
	case SPA_REAGENTCHANCE:
	case SPA_INCSPELLDMG: //Adjust for base2 used as base
		base = base2;
		break;
	}

	PITEMDB ItemDB = gItemDB;

	char extendedrange[MAX_STRING] = { 0 };
	char range[MAX_STRING] = { 0 };
	char repeating[MAX_STRING] = { 0 };
	char maxlevel[MAX_STRING] = { 0 };
	char spelleffectname[MAX_STRING] = { 0 };
	char extra[MAX_STRING] = { 0 };
	char maxtargets[MAX_STRING] = { 0 };
	char szPercent[MAX_STRING] = "%";

	GetSpellEffectName(spa, spelleffectname, sizeof(spelleffectname));
	strcpy_s(extra, pSpell->Extra);

	LONG minspelllvl = CalcMinSpellLevel(pSpell);
	LONG maxspelllvl = CalcMaxSpellLevel(calc, base, max, ticks, minspelllvl, level);
	LONG value = CalcValue(calc, (spa == SPA_STACKING_BLOCK) ? max : base, max, 1, minspelllvl, minspelllvl);
	LONG finish = CalcValue(calc, (spa == SPA_SPELLDAMAGETAKEN) ? base2 : base, max, ticks, minspelllvl, level);

	BOOL usePercent = (spa == SPA_MOVEMENTRATE || spa == SPA_HASTE || spa == SPA_BARDOVERHASTE || spa == SPA_SPELLDAMAGE || spa == SPA_HEALING || spa == SPA_DOUBLEATTACK || spa == SPA_STUNRESIST || spa == SPA_PROCMOD ||
		spa == SPA_DIVINEREZ || spa == SPA_METABOLISM || spa == SPA_TRIPLEBACKSTAB || spa == SPA_DOTCRIT || spa == SPA_HEALCRIT || spa == SPA_MENDCRIT || spa == SPA_FLURRY || spa == SPA_PETFLURRY ||
		spa == SPA_SPELLCRITCHANCE || spa == SPA_SHIELDBLOCKCHANCE || spa == SPA_DAMAGECRITMOD || spa == SPA_SPELLDAMAGETAKEN);
	BOOL AEEffect = (targettype == TT_PBAE || targettype == TT_TARGETED_AE || targettype == TT_AE_PC_V2 || targettype == TT_DIRECTIONAL);

	strcat_s(range, CalcValueRange(calc, base, max, ticks, minspelllvl, level, szTemp2, sizeof(szTemp2), usePercent ? szPercent : ""));
	strcat_s(extendedrange, CalcExtendedRange(calc, value, finish, minspelllvl, maxspelllvl, szTemp2, sizeof(szTemp2), usePercent ? szPercent : "", (spa == SPA_AC || spa == SPA_AC2)));
	if (ticks) sprintf_s(repeating, " per tick ");
	if (max) sprintf_s(maxlevel, " up to level %d", max);
	if (targets && AEEffect) sprintf_s(maxtargets, " on up to %d enemies", targets);

#ifdef DEBUGSPELLS
	WriteChatf("SLOT:%d, SPA:%d, BASE:%d, BASE2:%d, MAX:%d, CALC:%d, TICKS:%d, VALUE:%d, FINISH:%d, MINSPELLLVL:%d, MAXSPELLLVL:%d, RANGE:%s, EXTENDEDRANGE:%s, USEPERCENT:%s, REPEATING:%s, MAXLEVEL:%s",
		i + 1, spa, base, base2, max, calc, ticks, value, finish, minspelllvl, maxspelllvl, range, extendedrange, usePercent ? "TRUE" : "FALSE", repeating, maxlevel);
#endif

	sprintf_s(szBuff, "Slot %d: ", i + 1);
	switch (spa)
	{
	case 0: //hp +/-: heals/regen/dd 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(repeating)) strcat_s(szBuff, repeating);

		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		if (base2) {
			GetSpellRestrictions(pSpell, i, szTemp, sizeof(szTemp));
			strcat_s(szBuff, " -- Restrictions: ");
			strcat_s(szBuff, szTemp);
		}
		break;
	case 1: //ac mod 
	case 2: //attack mod 
	case 3: //movement speed mod 
	case 4: //str mod 
	case 5: //dex mod 
	case 6: //agi mod 
	case 7: //sta mod 
	case 8: //int mod 
	case 9: //wis mod 
	case 10: //cha mod 
	case 11: //haste mod 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 12: //Invisibility
		strcat_s(szBuff, spelleffectname);
		break;
	case 13: //See Invisible(c) 
	case 14: //Water Breathing(c)
		strcat_s(szBuff, FormatBase(spelleffectname, value, szTemp2));
		break;
	case 15: //mana +/-
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(repeating)) strcat_s(szBuff, repeating);
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		if (base2) {
			GetSpellRestrictions(pSpell, i, szTemp, sizeof(szTemp));
			strcat_s(szBuff, " -- Restrictions: ");
			strcat_s(szBuff, szTemp);
		}
		break;
	case 16: //NPC Frenzy (no spells currently)
	case 17: //NPC Awareness (no spells currently)
	case 18: //NPC Aggro
		strcat_s(szBuff, spelleffectname);
		break;
	case 19: //NPC Faction
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 20: //Blindness 
		strcat_s(szBuff, spelleffectname);
		break;
	case 21: //stun  time = base in ms 
		if (base2 != 0 && base != base2)
			sprintf_s(szTemp, " NPC for %1.fs (PC for %1.fs)%s", base / 1000.0f, base2 / 1000.0f, maxlevel);
		else
			sprintf_s(szTemp, " for %1.fs%s", base / 1000.0f, maxlevel);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 22: //Charm(c/level) 
	case 23: //Fear(c/level) 
		strcat_s(szBuff, FormatString(spelleffectname, maxlevel, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 24: //Fatigue 
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 25: //Bind Affinity 
		if (base == 2)
			strcat_s(szTemp, " (Secondary Bind Point)");
		if (base == 3)
			strcat_s(szTemp, " (Tertiary Bind Point)");
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 26: //Gate 
		if (base == 2)
			strcat_s(szTemp, " to Secondary Bind Point");
		if (base == 3)
			strcat_s(szTemp, " (Tertiary Bind Point)");
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 27: //Cancel Magic(c) 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 28: //Invisibility versus Undead 
	case 29: //Invisibility versus Animal 
		strcat_s(szBuff, spelleffectname);
		break;
	case 30: //NPC Aggro Radius(c/level) 
			 // NPC Aggro Radius' use the reverse sign from normal base values
		strcat_s(szBuff, FormatCount(spelleffectname, -value, szTemp2));
		strcat_s(szBuff, maxlevel);
		break;
	case 31: //Mesmerize(c/level) 
		strcat_s(szBuff, FormatString(spelleffectname, maxlevel, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 32: //Create Item
		while ((ItemDB) && (base != ItemDB->ID)) {
			ItemDB = ItemDB->pNext;
		}
		if (ItemDB) {
			sprintf_s(szTemp, "%s (Qty:%d)", ItemDB->szName, (LONG)ItemDB->StackSize < calc ? ItemDB->StackSize : calc);
		}
		else {
			sprintf_s(szTemp, "[%5d] (Qty:%d)", base, calc);
		}
		strcat_s(szBuff, FormatExtra(spelleffectname, szTemp, szTemp2));
		break;
	case 33: //Summon Pet
		strcat_s(szBuff, FormatExtra(spelleffectname, extra, szTemp2));
		break;
	case 34: //Confuse
		strcat_s(szBuff, spelleffectname);
		break;
	case 35: //disease counters 
	case 36: //poison counters 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 37: //DetectHostile (no spells currently)
	case 38: //DetectMagic (no spells currently)
	case 39: //No Twincast
	case 40: //Invulnerability 
	case 41: //Banish
	case 42: //Shadow Step
	case 43: //Berserk
	case 44: //Lycanthropy 
	case 45: //Vampirism 
		strcat_s(szBuff, spelleffectname);
		break;
	case 46: //fire resist 
	case 47: //cold resist 
	case 48: //poison resist 
	case 49: //disease resist 
	case 50: //magic resist 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 51: //Detect Traps (no spells currently)
	case 52: //Sense Undead 
	case 53: //Sense Summoned 
	case 54: //Sense Animals 
		strcat_s(szBuff, spelleffectname);
		break;
	case 55: //most runes 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 56: //True North 
		strcat_s(szBuff, spelleffectname);
		break;
	case 57: //Levitate(c) 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 58: //Illusion: Base=Race 
		strcat_s(szBuff, FormatExtra(spelleffectname, pEverQuest->GetRaceDesc(base), szTemp2));
		break;
	case 59: //Damage Shield 
			 // Damage Shield's use the reverse sign from normal base values
		strcat_s(szBuff, FormatRange(spelleffectname, -value, extendedrange, szTemp2));
		break;
	case 60: //Transfer Item (no spells currently)
	case 61: //Identify 
	case 62: //Item ID (no spells currently)
		strcat_s(szBuff, spelleffectname);
		break;
	case 63: //Memblur e% 
		strcat_s(szBuff, FormatPenaltyChance(spelleffectname, value + 40, szTemp2, szPercent, "Chance"));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 64: //SpinStun 
	case 65: //Infravision 
	case 66: //ultravision 
	case 67: //Eye of Zomm 
	case 68: //Reclaim Energy
		strcat_s(szBuff, spelleffectname);
		break;
	case 69: //max hp mod 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 70: //CorpseBomb (no spells currently)
		strcat_s(szBuff, spelleffectname);
		break;
	case 71: //Create Undead Pet
		strcat_s(szBuff, FormatExtra(spelleffectname, extra, szTemp2));
		break;
	case 72: //Preserve Corpse (no spells currently)
	case 73: //Bind Sight 
		strcat_s(szBuff, spelleffectname);
		break;
	case 74: //Feign Death 
		strcat_s(szBuff, FormatPenaltyChance(spelleffectname, value, szTemp2, szPercent, "Chance"));
		break;
	case 75: //Voice Graft 
	case 76: //Sentinel 
	case 77: //Locate Corpse 
		strcat_s(szBuff, spelleffectname);
		break;
	case 78: //Absorb Magic Damage 
	case 79: //+hp when cast (priest buffs that have heal component, DoTs with DDs) 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 80: //Enchant:Light (no spells currently)
		strcat_s(szBuff, spelleffectname);
		break;
	case 81: //Resurrect
		sprintf_s(szTemp, " and restore %d%s experience", value, szPercent);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 82: //Summon Player 
		strcat_s(szBuff, spelleffectname);
		break;
	case 83: //zone portal spells 
		if (targettype == 6) {
			sprintf_s(szTemp, " Self to %d, %d, %d in %s facing %s", GetSpellBase(pSpell, 0), GetSpellBase(pSpell, 1), GetSpellBase(pSpell, 2), GetFullZone(GetZoneID(extra)), szHeadingNormal[EQHeading(GetSpellBase(pSpell, 3))]);
		}
		else {
			sprintf_s(szTemp, " Group to %d, %d, %d in %s facing %s", GetSpellBase(pSpell, 0), GetSpellBase(pSpell, 1), GetSpellBase(pSpell, 2), GetFullZone(GetZoneID(extra)), szHeadingNormal[EQHeading(GetSpellBase(pSpell, 3))]);
		}
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 84: //Toss on Z axis 
		strcat_s(szBuff, FormatBase(spelleffectname, abs(base), base >= 0 ? " Down" : " Up", szTemp2));
		break;
	case 85: //Add Proc 
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 86: //Reaction Radius(c/level) 
			 // Reaction Radius' use the reverse sign from normal base values
		strcat_s(szBuff, FormatCount(spelleffectname, -value, szTemp2));
		strcat_s(szBuff, maxlevel);
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 87: //Perspective Magnification 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 88: //evac portal spells 
		sprintf_s(szTemp, " to %d, %d, %d in %s facing %s", GetSpellBase(pSpell, 0), GetSpellBase(pSpell, 1), GetSpellBase(pSpell, 2), extra, szHeadingNormal[EQHeading(GetSpellBase(pSpell, 3))]);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 89: //Player Size 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 90: //Cloak
	case 91: //Summon Corpse 
		strcat_s(szBuff, FormatString(spelleffectname, maxlevel, szTemp2));
		break;
	case 92: //hate mod 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 93: //Stop Rain 
	case 94: //Make Fragile 
	case 95: //Sacrifice 
		strcat_s(szBuff, spelleffectname);
		break;
	case 96: //Silence (no PC spells currently)
		strcat_s(szBuff, spelleffectname);
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 97: //Mana Pool 
	case 98: //Haste v2 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 99: //Root 
		strcat_s(szBuff, spelleffectname);
		break;
	case 100: //hp mod: pet heals/regen
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		strcat_s(szBuff, repeating);
		break;
	case 101: //Complete Heal (with duration)
	case 102: //Fearless 
	case 103: //Call Pet 
		strcat_s(szBuff, spelleffectname);
		break;
	case 104: //zone translocate spells
		if (extra[0])
			if (extra[0] == '0')
				strcat_s(szTemp, " to Bind Point");
			else
				sprintf_s(szTemp, " to %d, %d, %d in %s facing %s", GetSpellBase(pSpell, 0), GetSpellBase(pSpell, 1), GetSpellBase(pSpell, 2), GetFullZone(GetZoneID(extra)), szHeadingNormal[EQHeading(GetSpellBase(pSpell, 3))]);
		else
			strcat_s(szTemp, " to Bind Point");
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 105: //Anti-Gate 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 106: //Summon Warder 
		strcat_s(szBuff, FormatExtra(spelleffectname, extra, szTemp2));
		break;
	case 107: //Alter NPC Level (no spells currently)
		strcat_s(szBuff, spelleffectname);
		break;
	case 108: //Summon Familiar 
		strcat_s(szBuff, FormatExtra(spelleffectname, extra, szTemp2));
		break;
	case 109: //Summon Into Bag 
		while ((ItemDB) && (base != ItemDB->ID)) {
			ItemDB = ItemDB->pNext;
		}
		if (ItemDB) {
			sprintf_s(szTemp, "%s", ItemDB->szName);
		}
		else {
			sprintf_s(szTemp, "[%5d]", base);
		}
		strcat_s(szBuff, FormatExtra(spelleffectname, szTemp, szTemp2));
		break;
	case 110: //Increase Archery (no spells currently)
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 111: //Resistances
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 112: //Casting Level
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 113: //Summon Mount 
		strcat_s(szBuff, FormatExtra(spelleffectname, extra, szTemp2));
		break;
	case 114: //aggro multiplier 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 115: //Food/Water 
		strcat_s(szBuff, spelleffectname);
		break;
	case 116: //curse counters 
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 117: //Make Weapons Magical 
		strcat_s(szBuff, spelleffectname);
		break;
	case 118: //Singing Skill 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 119: //Bard Overhaste 
	case 120: //Reduce Healing Effectiveness (%) 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 121: //Reverse Damage Shield 
		strcat_s(szBuff, FormatBase(spelleffectname, -base, szTemp2));
		break;
	case 122: //Reduce Skill
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2));
		break;
	case 123: //Immunity
		strcat_s(szBuff, spelleffectname);
		break;
	case 124: //spell damage 
	case 125: //healing 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 126: //spell resist rate 
	case 127: //spell haste 
		strcat_s(szBuff, FormatPercent(spelleffectname, -value, -finish, szTemp2));
		break;
	case 128: //spell duration 
	case 129: //spell range 
	case 130: //spell/bash hate 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 131: //Decrease Chance of Using Reagent
	case 132: //Spell Mana Cost
	case 133: //Spell Stun Duration (no spells currently)
		strcat_s(szBuff, FormatPercent(spelleffectname, -value, -finish, szTemp2));
		break;
	case 134: //limit max level 
		if (base2 > 0)
			sprintf_s(szTemp, "%s (%d) (lose %d%s per level over cap)", spelleffectname, base, base2, szPercent);
		else
			strcpy_s(szTemp, FormatBase(spelleffectname, base, szTemp2));
		strcat_s(szBuff, szTemp);
		break;
	case 135: //Limit: Resist 
		strcat_s(szBuff, FormatLimits(spelleffectname, value, GetResistTypeName(base, szTemp), szTemp2));
		break;
	case 136: //limit target types this affects 
		strcat_s(szBuff, FormatLimits(spelleffectname, value, GetTargetTypeLimitsName(base, szTemp), szTemp2));
		break;
	case 137: //limit effect types this affects 
		strcat_s(szBuff, FormatLimits(spelleffectname, value, GetSpellEffectName(base, szTemp, sizeof(szTemp)), szTemp2));
		break;
	case 138: //limit spelltype this affects 
		strcat_s(szBuff, FormatLimits(spelleffectname, value, GetSpellTypeName(base, szTemp), szTemp2));
		break;
	case 139: //limit spell this affects 
		strcat_s(szBuff, FormatLimits(spelleffectname, value, GetSpellNameByID(base), szTemp2));
		break;
	case 140: //limit min duration of spells this affects (base= #ticks) 
		strcat_s(szBuff, FormatSeconds(spelleffectname, value * 6, szTemp2));
		break;
	case 141: //limit to instant spells only 
		strcat_s(szBuff, spelleffectname);
		break;
	case 142: //Limit: Min Level 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 143: //limit min casting time of spells this affects (base= seconds in ms) 
	case 144: //limit max casting time of spells this affects (base= seconds in ms) 
		strcat_s(szBuff, FormatSeconds(spelleffectname, value / 1000.0f, szTemp2));
		break;
	case 145: //Teleportv2 
		sprintf_s(szTemp, " to %d, %d, %d in %s facing %s", GetSpellBase(pSpell, 0), GetSpellBase(pSpell, 1), GetSpellBase(pSpell, 2), GetFullZone(GetZoneID(extra)), szHeadingNormal[EQHeading(GetSpellBase(pSpell, 3))]);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 146: //Resist Electricity
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 147: //Hit Points (% Max) 
		strcat_s(szBuff, FormatMax(spelleffectname, value, max, szTemp2));
		break;
	case 148: //Stacking: Block 
		strcat_s(szBuff, FormatStacking(spelleffectname, base2, value, /*(max>1000 ? max - 1000 : max)*/ max, spa, GetSpellEffectName(base, szTemp, sizeof(szTemp)), szTemp2));
		break;
	case 149: //Stacking: Overwrite 
		strcat_s(szBuff, FormatStacking(spelleffectname, calc - 200, value, (max > 1000 ? max - 1000 : max), spa, GetSpellEffectName(base, szTemp, sizeof(szTemp)), szTemp2));
		break;
	case 150: //Death Save - Restore Full Health 
		sprintf_s(szTemp, "Restore %s Health", base == 1 ? "Partial" : base == 2 ? "Full" : "Unknown");
		strcat_s(szBuff, FormatExtra(spelleffectname, szTemp, szTemp2));
		break;
	case 151: //Suspended Minion (no current spells)
		sprintf_s(szTemp, "(%s)", base == 0 ? "Current HP Only" : base == 1 ? "Current HP, Buffs, Weapons" : "Unknown");
		strcat_s(szBuff, FormatExtra(spelleffectname, szTemp, szTemp2));
		break;
	case 152: //Summon Pets (swarm) 
		sprintf_s(szTemp, "%s x%d for %dsec", extra, value, finish);
		strcat_s(szBuff, FormatExtra(spelleffectname, szTemp, szTemp2));
		break;
	case 153: //Balance Party Health 
		strcat_s(szBuff, FormatPenaltyChance(spelleffectname, value, szTemp2, szPercent, "Penalty"));
		break;
	case 154: //Remove Detrimental(c) 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 155: //PoP Resurrect
	case 156: //Illusion: Target 
		strcat_s(szBuff, spelleffectname);
		break;
	case 157: //Spell Damage Shield 
		strcat_s(szBuff, FormatRange(spelleffectname, -value, extendedrange, szTemp2));
		break;
	case 158: //Chance to Reflect Spell 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 159: //Stats 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 160: //Drunk effect 
		strcat_s(szBuff, FormatAT(spelleffectname, value, szTemp2, "if Alcholol Tolerance is below"));
		break;
	case 161: //Mitigate Spell Damage 
	case 162: //Mitigate Melee Damage 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, szTemp2, FALSE));
		if (max > 0)
			sprintf_s(szTemp, " until %d absorbed", max);
		strcat_s(szBuff, szTemp);
		break;
	case 163: //Absorb Damage 
		sprintf_s(szTemp, " up to %d from the next %d melee strikes or direct damage spells", max, value);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 164: //Attempt Sense (Cursed) Trap 
	case 165: //Attempt Disarm (Cursed) Trap 
	case 166: //Attempt Destroy (Cursed) Lock 
		strcat_s(szBuff, spelleffectname);
		break;
	case 167: //Increase Pet Power 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 168: //Mitigation 
		strcat_s(szBuff, FormatPercent(spelleffectname, -value, -finish, szTemp2));
		break;
	case 169: //Chance to Critical Hit 
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2, TRUE, "for"));
		break;
	case 170: //Chance to Critical Cast
	case 171: //Crippling Blow 
	case 172: //Melee Avoidance 
	case 173: //Riposte 
	case 174: //Dodge 
	case 175: //Parry 
	case 176: //Dual Wield 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 177: //Stat Cap Mod (how do they know which?) 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 178: //Lifetap Proc 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		strcat_s(szBuff, " heal");
		break;
	case 179: //Puretone 
	case 180: //Spell Resist 
	case 181: //Fearless 
	case 182: //Hundred Hands 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 183: //Skill Chance Mod
	case 184: //Chance to hit with Backstab (or throwing/archery [http://lucy.allakhazam.com/spellraw.html?id=9616&source=Live])
	case 185: //Damage Mod (how to tell which, rogues get a backstab only, others get an all skills) 
	case 186: //Damage Mod (see above) 
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2));
		break;
	case 187: //Mana Balance
		strcat_s(szBuff, FormatPenaltyChance(spelleffectname, value, szTemp2, szPercent, "Penalty"));
		break;
	case 188: //Block 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 189: //Endurance DoT/Regen 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		strcat_s(szBuff, repeating);
		break;
	case 190: //Max Endurance
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 191: //Amnesia
		strcat_s(szBuff, spelleffectname);
		break;
	case 192: //Discord Hate 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 193: //Skill Attack 
		strcat_s(szBuff, FormatSkillAttack(spelleffectname, value, base2, skill, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 194: //Fade 
		strcat_s(szBuff, spelleffectname);
		break;
	case 195: //Stun Resist 
	case 196: //Strikethrough 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 197: //Skill Damage 
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 198: //Endurance Heals 
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 199: //Taunt
		strcat_s(szBuff, spelleffectname);
		break;
	case 200: //Proc Mod 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 201: //Ranged Proc 
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 202: //Illusion Other
	case 203: //Mass Group Buff
		strcat_s(szBuff, spelleffectname);
		break;
	case 204: //War Cry 
		strcat_s(szBuff, FormatSeconds(spelleffectname, value, szTemp2, TRUE));
		break;
	case 205: //AE Rampage 
	case 206: //AE Taunt 
	case 207: //Flesh to Bone 
		strcat_s(szBuff, spelleffectname);
		break;
	case 208: //Purge Poison (no spells currently)
	case 209: //Disspell Beneficial Buffs 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 210: //Pet Shield 
		strcat_s(szBuff, FormatSeconds(spelleffectname, value * 1.0f, szTemp2));
		break;
	case 211: //AE Melee 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 212: //Frenzied Devastation (### come back and change ###)
		strcat_s(szBuff, FormatSeconds(spelleffectname, value, szTemp2, TRUE));
		break;
	case 213://Pet HP
	case 214: //Change Max HP
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2, FALSE, TRUE));
		break;
	case 215: //Pet Avoidance (no spells currently)
	case 216: //Accuracy 
	case 217: //Headshot (no spells currently)
	case 218: //Pet Crit Melee (no spells currently)
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 219: //Slay undead (Holyforge) 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 220: //Skill Damage Amt 
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2, FALSE));
		break;
	case 221: //Reduce Weight
	case 222: //Block Behind 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 223: //Double Riposte (no spells currently)
	case 224: //Additional Riposte
	case 225: //Double Attack 
	case 226: //2H Bash (no spells currently)
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 227: //Base Refresh Timer
		strcat_s(szBuff, FormatRefreshTimer(spelleffectname, -value, -finish, base2, szTemp2));
		break;
	case 228: //Reduce Fall Dmg (no spells currently)
	case 229: //Cast Through Stun (no spells currently)
	case 230: //Increase Shield Dist (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 231: //Stun Bash Chance (no spells currently)
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 232: //Divine Save
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 233: //Metabolism
		strcat_s(szBuff, FormatRange(spelleffectname, -value, extendedrange, szTemp2));
		break;
	case 234: //Poison Mastery (no spells currently)
	case 235: //Focus Channelling (no spells currently)
	case 236: //Free Pet (no spells currently)
	case 237: //Pet Affinity (no spells currently)
	case 238: //Permanent Illusion (no spells currently)
	case 239: //Stonewall (no spells currently)
	case 240: //String Unbreakable (no spells currently)
	case 241: //Improve Reclaim Energy (no spells currently)
	case 242: //Increase Chance Memwipe (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 243: //NoBreak Charm Chance
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 244: //Root Break Chance
	case 245: //Trap Circumvention (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 246: //Lung Capacity
		strcat_s(szBuff, FormatRange(spelleffectname, value, range, szTemp2));
		break;
	case 247: //Increase SkillCap (no spells currently)
	case 248: //Extra Specialization (no spells currently)
	case 249: //Offhand Min (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 250: //Spell Proc Chance
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 251: //Endless Quiver (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 252: //Backstab from Front
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 253: //Chaotic Stab (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 254: //placeholder of some kind 
		break;
	case 255: //Shielding Duration (no spells currently)
		strcat_s(szBuff, FormatBasePercent(spelleffectname, base, szTemp2));
		break;
	case 256: //Shroud Of Stealth (no spells currently)
	case 257: //Give Pet Hold (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 258: //Triple Backstab 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 259: //AC Limit
	case 260: //Add Instrument 
	case 261: //Song Cap (no spells currently)
		strcat_s(szBuff, FormatBasePercent(spelleffectname, base, szTemp2));
		break;
	case 262: //Stats Cap
		strcat_s(szBuff, FormatStatsCapRange(spelleffectname, value, GetStatShortName(base2, szTemp), extendedrange, szTemp2));
		break;
	case 263: //Tradeskill Masteries (no spells currently)
	case 264: //Reduce AATimer
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 265: //No Fizzle
		strcat_s(szBuff, spelleffectname);
		break;
	case 266: //Attack Chance 
	case 267: //Add Pet Commands (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 268: //TS Fail Rate
		strcat_s(szBuff, FormatSkills(spelleffectname, -value, -finish, base2, szTemp2, TRUE, "for"));
		break;
	case 269: //Bandage Perc Limit (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 270: //Bard Song Range
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2, "to"));
		break;
	case 271: //Base Run Speed
		strcat_s(szBuff, FormatBasePercent(spelleffectname, base, szTemp2));
		break;
	case 272: //Casting Level
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 273: //DoT Crit
	case 274: //Heal Crit 
	case 275: //Mend Crit (no spells currently)
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 276: //Dual Wield Amt (no spells currently)
	case 277: //Extra DI Chance (no spells currently)
	case 278: //Finishing Blow (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 279: //Flurry 
	case 280: //Pet Flurry Chance
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 281: //Give Pet Feign (no spells currently)
	case 282: //Increase Bandage Amt (no spells currently)
	case 283: //Special Attack Chain (no spells currently)
	case 284: //LoH Set Heal (no spells currently)
	case 285: //NoMove Check Sneak (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 286: //DD Bonus
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 287: //Focus Combat Duration
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		strcat_s(szBuff, " tick(s)");
		break;
	case 288: //Add Proc Hit (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 289: //Trigger on Fade 
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Fade"));
		break;
	case 290: //Increase Movement Cap (no spells currently)
	case 291: //Purify
	case 292: //Strikethrough2
	case 293: //StunResist2 (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 294: //Spell Crit Chance
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 295: //Reduce Timer Special (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 296: //Incoming Spell Damage
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 297: //Incoming Spell Damage Amt
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 298: //Tiny Companion
		strcat_s(szBuff, FormatPercent(spelleffectname, -value, -finish, szTemp2));
		break;
	case 299: //Wake the Dead 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 300: //Doppleganger
		strcat_s(szBuff, spelleffectname);
		break;
	case 301: //Increase Range Damage (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 302: //Damage Crit
	case 303: //Damage
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 304: //Secondary Riposte Mod (no spells currently)
		strcat_s(szBuff, FormatBasePercent(spelleffectname, base, szTemp2));
		break;
	case 305: //Damage Shield Mitigation
		strcat_s(szBuff, FormatPercent(spelleffectname, -value, -finish, szTemp2));
		break;
	case 306: //Army of Dead 
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 307: //Appraisal
	case 308: //Suspend Minion 
	case 309: //Teleport Bind
		strcat_s(szBuff, spelleffectname);
		break;
	case 310: //Reuse Timer 
		strcat_s(szBuff, FormatTimer(spelleffectname, -base / 1000.0f, szTemp2));
		break;
	case 311: //No Combat Skills 
		strcat_s(szBuff, spelleffectname);
		break;
	case 312: //Sanc 
	case 313: //Forage Master (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 314: //Improved Invisibility
	case 315: //Improved Invisibility Vs Undead
	case 316: //Improved Invisibility Vs Animals (no spells currently)
		strcat_s(szBuff, spelleffectname);
		break;
	case 317: //Worn Regen Cap
	case 318: //Worn Mana Cap
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 319: //Critical HP Regen
	case 320: //Shield Block Chance
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 321: //Soothing 
		strcat_s(szBuff, FormatBasePercent(spelleffectname, base, szTemp2));
		break;
	case 322: //Origin 
		strcat_s(szBuff, spelleffectname);
		break;
	case 323: //Add Defensive Proc 
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 324: //Spirit Channel
		strcat_s(szBuff, FormatBasePercent(spelleffectname, base, szTemp2));
		break;
	case 325: //No Break AE Sneak (no spells currently)
	case 326: //Spell Slots (no spells currently)
	case 327: //Buff Slots (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 328: //Negative HP Limit
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 329: //Mana Shield
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2, "up to", szPercent));
		break;
	case 330: //Crit Damage 
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2));
		break;
	case 331: //Item Recovery
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 332: //Summon to Corpse 
		strcat_s(szBuff, spelleffectname);
		break;
	case 333: //Trigger on fade 
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Fade"));
		break;
	case 334: //Song DoT 
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		strcat_s(szBuff, repeating);
		strcat_s(szBuff, " if target is not moving");
		break;
	case 335: //Fc_Immunity Focus
		strcat_s(szBuff, spelleffectname);
		break;
	case 336: //Illusionary Target (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 337: //Experience buff 
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 338: //Expedient Recovery
		strcat_s(szBuff, spelleffectname);
		break;
	case 339: //Trigger DoT on cast
	case 340: //Trigger DD on cast 
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatSpellChance(spelleffectname, base, base2, szTemp), szTemp2, " on Cast"));
		break;
	case 341: //Worn Attack Cap
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 342: //Prevent Flee on Low Health
		strcat_s(szBuff, spelleffectname);
		break;
	case 343: //Spell Interrupt
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 344: //Item Channeling (no spells currently)
	case 345: //Assassinate Max (no spells currently)
	case 346: //Headshot Max (no spells currently)
	case 347: //Double Ranged Attack (no spells currently)
	case 348: //Limit: Mana Min
	case 349: //Increase Damage With Shield (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 350: //Manaburn
		strcat_s(szBuff, FormatCount(spelleffectname, value * 4, szTemp2, "for"));
		break;
	case 351: //Persistent Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(id + (spellgroup ? 3 : 1)), szTemp2));
		break;
	case 352: //Increase Trap Count
	case 353: //Increase SOI Count
	case 354: //Deactivate All Traps
	case 355: //Learn Trap
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 356: //Change Trigger Type (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 357: //Mute
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 358: //Mana/Max Mana
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 359: //Passive Sense Trap
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 360: //Killshot Triggers
	case 361: //Proc On Death
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatSpellChance(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 362: //Potion Belt (no spells currently)
	case 363: //Bandolier (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 364: //Triple Attack Chance
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 365: //Trigger on Kill Shot 
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatSpellChance(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 366: //Group Shielding
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 367: //Change Body Type 
		sprintf_s(szTemp, " to %s", base == 25 ? "Plant" : base == 21 ? "Animal" : base == 3 ? "Undead" : "Unknown");
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 368: //Modify Faction
		strcat_s(szBuff, FormatExtra(spelleffectname, GetFactionName(base, szTemp), szTemp2));
		break;
	case 369: //Corruption Counters 
	case 370: //Corruption Resists 
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 371: //Slow
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 372: //Grant Foraging (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 373: //Trigger Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Fade"));
		break;
	case 374: //Trigger Spell
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatSpellChance(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 375: //Critical DoT Damage Mod
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 376: //Fling
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 377: //Trigger Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Fade"));
		break;
	case 378: //Resist
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatResists(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 379: //Directional Shadowstep
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 380: //Knockback Explosive
		sprintf_s(szTemp, " (%d) and Toss Up (%d)", base, base2);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 381: //Fling to Self
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 382: //Negate: Effect
	{
		char szString[MAX_STRING] = { 0 };
		sprintf_s(szTemp, " %s Effect", GetSpellEffectNameByID(base2, szString, MAX_STRING));
		strcat_s(szBuff, FormatExtra(spelleffectname, szTemp, szTemp2));
		break;
	}
	case 383: //Trigger Spell
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatSpellChance(spelleffectname, base, base2, szTemp), szTemp2, " on Cast"));
		break;
	case 384: //Fling to Target
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 385: //Limit: SpellGroup
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameBySpellGroupID(base), szTemp2));
		break;
	case 386: //Trigger Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Curer"));
		break;
	case 387: //Trigger Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Fade"));
		break;
	case 388: //Summon All Corpses
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 389: //Spell Gem Refresh
		strcat_s(szBuff, FormatCount(spelleffectname, -value, szTemp2, "to"));
		break;
	case 390: //Fc_Timer Lockout
	case 391: //Limit: Mana Max
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 392: //Heal Amt
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 393: //Incoming Healing Effectiveness
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 394: //Incoming Healing Amt
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 395: //Fc_Heal % Crit (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 396: //Heal Amt
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 397: //Pet Amt Mitigation (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 398: //Swarm Pet Duration
		strcat_s(szBuff, FormatSecondsCount(spelleffectname, value / 1000.0f, szTemp2));
		break;
	case 399: //Twincast Chance
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 400: //Healburn
		sprintf_s(szTemp, " use up to %d mana to heal your group", value);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 401: //Mana/HP
	case 402: //Endurance/HP
		strcat_s(szBuff, FormatCount(spelleffectname, -value, szTemp2, "by up to"));
		break;
	case 403: //Limit: SpellClass
	case 404: //Limit: SpellSubclass
	case 405: //Staff Block Chance (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 406: //Trigger Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Max Hits"));
		break;
	case 407: //Trigger Effect
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Focus Used"));
		break;
	case 408: //Limit HP
	case 409: //Limit Mana
	case 410: //Limit Endurance
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2, "to"));
		break;
	case 411: //Limit: PlayerClass
		strcat_s(szBuff, FormatExtra(spelleffectname, GetClassesFromMask(base, szTemp), szTemp2));
		break;
	case 412: //Limit: Race (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2)); // needs work (base2 bitmask of races)
		break;
	case 413: //Base Dmg
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2, "by", szPercent));
		break;
	case 414: //Limit: CastingSkill
	case 415: //Limit: ItemClass (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 416: //AC2
	case 417: //Mana2
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2));
		break;
	case 418: //Increased Skill Damage2
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 419: //Add Proc
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 420: //Fc_Limit Use (no spells currently)
	case 421: //Fc_Limit Use Amt (no spells currently)
	case 422: //Limit: Use Min (no spells currently)
	case 423: //Limit: Use Type (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 424: //Gravitate
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		if (strlen(maxtargets)) strcat_s(szBuff, maxtargets);
		break;
	case 425: //Fly
		strcat_s(szBuff, spelleffectname);
		break;
	case 426: //AddExtTargetSlots (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 427: //Skill Proc
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 428: //Limit Skill
		strcat_s(szBuff, FormatExtra(spelleffectname, base >= 0 ? szSkills[base] : "All Skills", szTemp2));
		break;
	case 429: //Skill Proc Success
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 430: //PostEffect
	case 431: //PostEffectData
	case 432: //ExpandMaxActiveTrophyBenefits (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 433: //Skill Min Damage
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatRateMod(spelleffectname, base, base2, szTemp), szTemp2));
		break;
	case 434: //Skill Min Damage
	case 435: //Fragile Defense
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 436: //Beneficial Countdown Hold
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 437: //Teleport to Anchor
	case 438: //Translocate to Anchor
		sprintf_s(szTemp, " to %s Anchor", base == 50874 ? "Guild Hall" : base == 52584 ? "Primary" : base == 52585 ? "Secondary" : "Unknown");
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 439: //Assassinate (no spells currently)
	case 440: //FinishingBlowMax (no spells currently)
	case 441: //Distance Removal
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 442: //Doom Req Target
	case 443: //Doom Req Caster
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2));
		if (base2) {
			GetSpellRestrictions(pSpell, i, szTemp, sizeof(szTemp));
			strcat_s(szBuff, " -- Restrictions: ");
			strcat_s(szBuff, szTemp);
		}
		break;
	case 444: //Improved Taunt
		sprintf_s(szTemp, " up to L%d and Reduce Ally Hate Generation by %d%s", base, base2, szPercent);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 445: //Add Merc Slot
	case 446: //A_Stacker
	case 447: //B_Stacker
	case 448: //C_Stacker
	case 449: //D_Stacker
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 450: //DoT Guard
		sprintf_s(szTemp, " absorbing %d%s damage to a total of %d", value, szPercent, max);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 451: //Melee Threshold Guard
	case 452: //Spell Threshold Guard
		sprintf_s(szTemp, " absorbing %d%s of incoming %s damage in excess of %d to a total of %d", value, szPercent, spa == 451 ? "melee" : "spell", base2, max);
		strcat_s(szBuff, FormatString(spelleffectname, szTemp, szTemp2));
		break;
	case 453: //Doom Melee Threshold
		sprintf_s(szTemp, " on %d Melee Damage Taken", base2);
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, szTemp));
		break;
	case 454: //Doom Spell Threshold
		sprintf_s(szTemp, " on %d Spell Damage Taken", base2);
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, szTemp));
		break;
	case 455: //Add Hate %
	case 456: //Add Hate Over Time %
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 457: //Resource Tap
		sprintf_s(szTemp, "Return %.2f%s of direct damage as %s", value / 10.0f, szPercent, base2 == 0 ? "hit points" : base2 == 1 ? "mana" : base2 == 2 ? "endurance" : "unknown");
		strcat_s(szBuff, szTemp);
		break;
	case 458: //Faction Mod %
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 459: //Damage Mod 2 (how to tell which, rogues get a backstab only, others get an all skills) 
		strcat_s(szBuff, FormatSkills(spelleffectname, value, finish, base2, szTemp2));
		break;
	case 460: //Limit: Include Non-Focusable
		strcat_s(szBuff, spelleffectname);
		break;
	case 461: //Fc Damage % 2 (no spells currently)
	case 462: //Fc Damage Amt 2 (no spells currently)
	case 463: //Shield Target (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 464: //PC Pet Rampage
	case 465: //PC Pet AE Rampage
	case 466: //PC Pet Flurry Chance
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 467: //DS Mitigation Amt
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 468: //DS Mitigation Percentage
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 469: //Chance Best in Spell Group
	case 470: //Trigger Best in Spell Group
		strcat_s(szBuff, FormatExtra(spelleffectname, FormatSpellGroupChance(spelleffectname, base, base2, szTemp), szTemp2, " on Cast"));
		break;
	case 471: //Double Melee Round (PC Only)
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 472: //Toggle Passive AA Rank
		strcat_s(szBuff, spelleffectname);
		break;
	case 473: //Double Backstab From Front  (no spells currently)
	case 474: //Pet Crit Melee Damage% (Owner)  (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 475: //Trigger Spell Non-Item
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Cast"));
		break;
	case 476: //Weapon Stance (no spells currently)
	case 477: //Move to Top of Hatelist (no spells currently)
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 478: //Move to Bottom of Hatelist
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base), szTemp2, " on Cast"));
		break;
	case 479: //Value Min
		sprintf_s(szTemp, "%s %s", spelleffectname, base < 0 ? "Max" : "Min");
		strcat_s(szBuff, FormatMinMaxBase(szTemp, base, base2, szTemp2));
		break;
	case 480: //Value Max
		sprintf_s(szTemp, "%s %s", spelleffectname, base < 0 ? "Min" : "Max");
		strcat_s(szBuff, FormatMinMaxBase(szTemp, base, base2, szTemp2));
		break;
	case 481: //Cast Spell on Land
		strcat_s(szBuff, FormatExtra(spelleffectname, GetSpellNameByID(base2), szTemp2, " on Land and conditions are met"));
		break;
	case 482: //Skill Base Damage Mod
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 483: //Spell Damage Taken
	case 484: //Spell Damage Taken
		strcat_s(szBuff, FormatRange(spelleffectname, value, extendedrange, szTemp2, " (after crit)"));
		break;
	case 485: //CasterClass
		strcat_s(szBuff, FormatExtra(spelleffectname, GetClassesFromMask(base, szTemp), szTemp2));
		break;
	case 486: //Same Caster
		strcat_s(szBuff, FormatExtra(spelleffectname, base ? "(Same)" : "(Different)", szTemp2, "", ""));
		break;
	case 487: //Extend Tradeskill Cap
		sprintf_s(szTemp, "%s (%d, %d, %d)", spelleffectname, base, base2, max);
		strcat_s(szBuff, szTemp);
		break;
	case 488: //Push Taken
		strcat_s(szBuff, FormatBase(spelleffectname, -base, szTemp2));
		break;
	case 489: //Worn Endurance Regen Cap
		strcat_s(szBuff, FormatBase(spelleffectname, base, szTemp2));
		break;
	case 490: //Limit: ReuseTime Min
	case 491: //Limit: ReuseTime Max
		strcat_s(szBuff, FormatSeconds(spelleffectname, value / 1000.0f, szTemp2));
		break;
	case 492: //Limit: Endurance Min
	case 493: //Limit: Endurance Max
	case 494: //Pet Add Attack
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 495: //Limit: Duration Max
		strcat_s(szBuff, FormatSeconds(spelleffectname, value * 6, szTemp2));
		break;
	case 496: //Critical Hit Damage (Non-stacking)
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		strcat_s(szBuff, " of Base Damage (Non Stacking)");
		break;
	case 497: //NoProc
		sprintf_s(szTemp, "%s (%d, %d, %d)", spelleffectname, base, base2, max);
		strcat_s(szBuff, szTemp);
		break;
	case 498: //Extra Attack % (1H Primary)
	case 499: //Extra Attack % (1H Secondary)
	case 500: //Spell Haste v2
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		break;
	case 501: //Spell Cast Time
	case 502: //Stun and Fear
		strcat_s(szBuff, FormatSeconds(spelleffectname, value / 1000.0f, szTemp2));
		break;
	case 503: //Rear Arc Melee Damage Mod
		strcat_s(szBuff, FormatSeconds(spelleffectname, value / 10.0f, szTemp2));
		break;
	case 504: //Rear Arc Melee Damage
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 505: //Rear Arc Damage Taken Mod
		strcat_s(szBuff, FormatSeconds(spelleffectname, value / 10.0f, szTemp2));
		break;
	case 506: //Rear Arc Damage Taken
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 507: //Spell Damage v4 Mod
		strcat_s(szBuff, FormatPercent(spelleffectname, value, finish, szTemp2));
		strcat_s(szBuff, " (Before DoT Crit, After Nuke Crit)");
		break;
	case 508: //Spell Damage v4
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	case 509: //Health Transfer
		sprintf_s(szTemp, "%s (%d, %d, %d)", spelleffectname, base, base2, max);
		strcat_s(szBuff, szTemp);
		break;
	case 510: //Resist Incoming
		strcat_s(szBuff, FormatCount(spelleffectname, value, szTemp2));
		break;
	default: //undefined effect
		sprintf_s(szTemp, "%s (base=%d, base2=%d, max=%d, calc=%d, value=%d)", spelleffectname, base, base2, max, calc, value);
		strcat_s(szBuff, szTemp);
		break;
	}

#ifdef DEBUGSPELLS
	if (strlen(szBuff) > 0) WriteChatf("%s", szBuff);
#endif
	strcat_s(szBuffer, BufferSize, szBuff);
	return szBuffer;
}

char* ShowSpellSlotInfo(PSPELL pSpell, char* szBuffer, size_t BufferSize)
{
	char szTemp[MAX_STRING] = { 0 };
	char szBuff[MAX_STRING] = { 0 };
	int count = 0;
	for (int i = 0; i < GetSpellNumEffects(pSpell); i++)
	{
		szBuff[0] = szTemp[0] = '\0';
		strcat_s(szBuff, ParseSpellEffect(pSpell, i, szTemp, sizeof(szTemp)));
		size_t len = strlen(szBuff);
		if (len > 0 && count + len < BufferSize) {
			strcat_s(szBuffer, BufferSize, szBuff);
			strcat_s(szBuffer, BufferSize, "<br>");
		}
		count += len + 4;
	}
	return szBuffer;
}

void SlotValueCalculate(char* szBuff, PSPELL pSpell, int i, double mp)
{
	sprintf_s(szBuff, 12, "%d", CalcValue(GetSpellCalc(pSpell, i), GetSpellBase(pSpell, i), GetSpellMax(pSpell, i), pSpell->DurationCap));
	return;
}

void DisplayOverlayText(char* szText, DWORD dwColor, DWORD dwTransparency, DWORD msFadeIn, DWORD msFadeOut, DWORD msHold)
{
	CBroadcast* pBC = GetTextOverlay();
	if (!pBC) {
		WriteChatColor(szText, dwColor);
		return;
	}
	DWORD dwAlpha = (DWORD)(dwTransparency * 255 / 100);
	if (dwAlpha > 255) dwAlpha = 255;

	((CTextOverlay*)pBC)->DisplayText(
		szText,
		dwColor,
		10, // Always 10 in eqgame.exe,
			// Doesn't seem to affect anything
			// (tried 0,1,10,20,100,500)
		dwAlpha,
		msFadeIn,
		msFadeOut,
		msHold);
}

void CustomPopup(char* szPopText, bool bPopOutput)
{
	int  iArgNum = 1;
	int  iMsgColor = CONCOLOR_LIGHTBLUE;
	int  iMsgTime = 3000;
	char szCurArg[MAX_STRING] = { 0 };
	char szPopupMsg[MAX_STRING] = { 0 };
	char szErrorCust[MAX_STRING] = "\awUsage: /popcustom [\agcolor\ax] [\agdisplaytime\ax(in seconds)] [\agmessage\ax]";
	char szErrorEcho[MAX_STRING] = "\awUsage: /popupecho [\agcolor\ax] [\agdisplaytime\ax(in seconds)] [\agmessage\ax]";

	GetArg(szCurArg, szPopText, iArgNum++);
	if (!*szCurArg)
	{
		if (bPopOutput)
		{
			WriteChatf("%s", szErrorEcho);
		}
		else
		{
			WriteChatf("%s", szErrorCust);
		}
		return;
	}
	else
	{
		if (isdigit(szCurArg[0]))
		{
			iMsgColor = atoi(szCurArg);
			GetArg(szCurArg, szPopText, iArgNum++);
			if (isdigit(szCurArg[0]))
			{
				iMsgTime = atoi(szCurArg) * 1000;
				sprintf_s(szPopupMsg, "%s", GetNextArg(szPopText, 2, FALSE, 0));
			}
			else
			{
				sprintf_s(szPopupMsg, "%s", GetNextArg(szPopText, 1, FALSE, 0));
			}
		}
		else
		{
			strcpy_s(szPopupMsg, szPopText);
		}
	}
	DisplayOverlayText(szPopupMsg, iMsgColor, 100, 500, 500, iMsgTime);
	if (bPopOutput) WriteChatf("\ayPopup\aw:: %s", szPopupMsg);
}

BOOL ParseKeyCombo(char* text, KeyCombo& Dest)
{
	KeyCombo Ret;
	if (!_stricmp(text, "clear"))
	{
		Dest = Ret;
		return true;
	}
	char Copy[MAX_STRING];
	strcpy_s(Copy, text);
	char* token1 = NULL;
	char* next_token1 = NULL;

	token1 = strtok_s(Copy, "+ ", &next_token1);
	while (token1 != NULL)
	{
		if (token1 != NULL)
		{
			if (!_stricmp(token1, "alt"))
				Ret.Data[0] = 1;
			else if (!_stricmp(token1, "ctrl"))
				Ret.Data[1] = 1;
			else if (!_stricmp(token1, "shift"))
				Ret.Data[2] = 1;
			else
			{
				for (unsigned long i = 0; gDiKeyID[i].Id; i++)
				{
					if (!_stricmp(token1, gDiKeyID[i].szName))
					{
						Ret.Data[3] = (char)gDiKeyID[i].Id;
						break;
					}
				}
			}
			token1 = strtok_s(NULL, "+ ", &next_token1);
		}
	}
	if (Ret.Data[3])
	{
		Dest = Ret;
		return true;
	}
	return false;
}

char* DescribeKeyCombo(KeyCombo& Combo, char* szDest, size_t BufferSize)
{
	unsigned long pos = 0;
	szDest[0] = 0;
	if (Combo.Data[2])
	{
		strcpy_s(&szDest[pos], BufferSize - pos, "shift");
		pos += 5;
	}
	if (Combo.Data[1])
	{
		if (pos)
		{
			szDest[pos] = '+';
			pos++;
		}
		strcpy_s(&szDest[pos], BufferSize - pos, "ctrl");
		pos += 4;
	}
	if (Combo.Data[0])
	{
		if (pos)
		{
			szDest[pos] = '+';
			pos++;
		}
		strcpy_s(&szDest[pos], BufferSize - pos, "alt");
		pos += 3;
	}
	if (pos)
	{
		szDest[pos] = '+';
		pos++;
	}
	if (Combo.Data[3])
	{
		strcpy_s(&szDest[pos], BufferSize - pos, gDiKeyName[Combo.Data[3]]);
	}
	else
	{
		strcpy_s(&szDest[pos], BufferSize - pos, "clear");
	}
	return &szDest[0];
}

BOOL LoadCfgFile(char* Filename, BOOL Delayed)
{
	FILE* file = 0;
	errno_t err = 0;
	char szFilename[MAX_STRING] = { 0 };
	strcpy_s(szFilename, Filename);
	if (!strchr(szFilename, '.'))
		strcat_s(szFilename, ".cfg");
	char szFull[MAX_STRING] = { 0 };
#define TryFile(name)  \
    {\
	if((err = fopen_s(&file,name,"rt"))==0)\
    goto havecfgfile;\
    }
	sprintf_s(szFull, "%s\\Configs\\%s", gszINIPath, szFilename);
	TryFile(szFull);
	sprintf_s(szFull, "%s\\%s", gszINIPath, szFilename);
	TryFile(szFull);
	TryFile(szFilename);
	TryFile(Filename);
#undef TryFile
	return false;
havecfgfile:
	char szBuffer[MAX_STRING] = { 0 };
	while (fgets(szBuffer, MAX_STRING, file))
	{
		char* Next_Token1 = 0;
		char* Cmd = strtok_s(szBuffer, "\r\n", &Next_Token1);
		if (Cmd && Cmd[0] && Cmd[0] != ';')
		{
			HideDoCommand(((PSPAWNINFO)pLocalPlayer), Cmd, Delayed);
		}
	}
	fclose(file);
	return true;
}

int FindInvSlotForContents(CONTENTS* pContents)
{
	int LastMatch = -1;

	// screw the old style InvSlot numbers
	// return the index into the INVSLOTMGR array
	DebugSpew("FindInvSlotForContents(0x%08X) (0x%08X)", pContents, GetItemFromContents(pContents));

#if 1
	for (unsigned long N = 0; N < MAX_INV_SLOTS; N++)
	{
		CONTENTS* pC = nullptr;

		if (pInvSlotMgr->SlotArray[N]) {
			CInvSlot* pCIS = pInvSlotMgr->SlotArray[N];
			pCIS->GetItemBase(&pC);

			if (pC)
			{
				DebugSpew("pInvSlotMgr->SlotArray[%d] Contents==0x%08X", N, pC);
				if (pC == pContents)
				{
					CInvSlot* pInvSlot = pInvSlotMgr->SlotArray[N];

					if (pInvSlot->pInvSlotWnd)
					{
						DebugSpew("%d slot %d wnd %d %d %d", N,
							pInvSlot->Index,
							pInvSlot->pInvSlotWnd->ItemLocation.GetLocation(),
							pInvSlot->pInvSlotWnd->ItemLocation.GetIndex().GetSlot(0),
							pInvSlot->pInvSlotWnd->ItemLocation.GetIndex().GetSlot(1)
						);
					}

					if (pInvSlot->pInvSlotWnd
						&& pInvSlot->pInvSlotWnd->ItemLocation.GetLocation() == eItemContainerPossessions)
					{
						return pInvSlot->Index;
					}
					else if (pInvSlot->pInvSlotWnd && pInvSlot->pInvSlotWnd->ItemLocation.GetIndex().GetSlot(1) != -1)
					{
						return pInvSlot->Index;
					}
					else if (pInvSlot->pInvSlotWnd
						&& pInvSlot->pInvSlotWnd->ItemLocation.GetLocation() == eItemContainerCorpse)
					{
						// loot window items should not be anywhere else
						return pInvSlot->Index;
					}
					else
					{
						LastMatch = N;
					}
				}
			}
		}
	}

	// return specific window type if needed
	if (LastMatch != -1 && pInvSlotMgr->SlotArray[LastMatch]->pInvSlotWnd->ItemLocation.GetLocation() == 9999)
		return  pInvSlotMgr->SlotArray[LastMatch]->Index;
#endif
	return -1;
}

DWORD LastFoundInvSlot = -1;

int FindInvSlot(char* pName, BOOL Exact)
{
	char Name[MAX_STRING] = { 0 };
	strcpy_s(Name, pName);
	_strlwr_s(Name);
	char szTemp[MAX_STRING] = { 0 };

	for (unsigned long N = 0; N < MAX_INV_SLOTS; N++)
	{
		if (pInvSlotMgr->SlotArray[N])
		{
			CInvSlot* x = pInvSlotMgr->SlotArray[N];
			CONTENTS* y = nullptr;

			if (x)
			{
				x->GetItemBase(&y);
			}

			if (y)
			{
				if (!Exact)
				{
					strcpy_s(szTemp, GetItemFromContents(y)->Name);
					_strlwr_s(szTemp);
					if (strstr(szTemp, Name))
					{
						if (pInvSlotMgr->SlotArray[N]->pInvSlotWnd)
						{
							LastFoundInvSlot = N;
							return pInvSlotMgr->SlotArray[N]->Index;
						}
						// let it try to find it in an open slot if this fails
					}
				}
				else if (!_stricmp(Name, GetItemFromContents(y)->Name))
				{
					if (pInvSlotMgr->SlotArray[N]->pInvSlotWnd)
					{
						LastFoundInvSlot = N;
						return pInvSlotMgr->SlotArray[N]->Index;
					}
					// let it try to find it in an open slot if this fails
				}

			}
		}
	}
	LastFoundInvSlot = -1;
	return -1;
}

int FindNextInvSlot(char* pName, BOOL Exact)
{
	char szTemp[MAX_STRING] = { 0 };
	char Name[MAX_STRING] = { 0 };
	strcpy_s(Name, pName);
	_strlwr_s(Name);

#if 0
	PEQINVSLOTMGR pInvMgr = (PEQINVSLOTMGR)pInvSlotMgr;
	for (unsigned long N = LastFoundInvSlot + 1; N < MAX_INV_SLOTS; N++)
	{
		if (pInvMgr->SlotArray[N])
		{
			if (pInvMgr->SlotArray[N]->ppContents && *pInvMgr->SlotArray[N]->ppContents)
			{
				if (!Exact)
				{
					__strlwr_s(strcpy_s(szTemp, (*pInvMgr->SlotArray[N]->ppContents)->Item->Name));
					if (strstr(szTemp, Name))
					{
						if (pInvMgr->SlotArray[N]->pInvSlotWnd)
						{
							LastFoundInvSlot = N;
							return pInvMgr->SlotArray[N]->pInvSlotWnd->InvSlot;
						}
						// let it try to find it in an open slot if this fails
					}
				}
				else if (!_stricmp(Name, (*pInvMgr->SlotArray[N]->ppContents)->Item->Name))
				{
					if (pInvMgr->SlotArray[N]->pInvSlotWnd)
					{
						LastFoundInvSlot = N;
						return pInvMgr->SlotArray[N]->pInvSlotWnd->InvSlot;
					}
					// let it try to find it in an open slot if this fails
				}

			}
		}
	}
#endif
	LastFoundInvSlot = -1;
	return -1;
}

enum eCalcOp
{
	CO_NUMBER = 0,
	CO_OPENPARENS = 1,
	CO_CLOSEPARENS = 2,
	CO_ADD = 3,
	CO_SUBTRACT = 4,
	CO_MULTIPLY = 5,
	CO_DIVIDE = 6,
	CO_IDIVIDE = 7,
	CO_LAND = 8,
	CO_AND = 9,
	CO_LOR = 10,
	CO_OR = 11,
	CO_XOR = 12,
	CO_EQUAL = 13,
	CO_NOTEQUAL = 14,
	CO_GREATER = 15,
	CO_NOTGREATER = 16,
	CO_LESS = 17,
	CO_NOTLESS = 18,
	CO_MODULUS = 19,
	CO_POWER = 20,
	CO_LNOT = 21,
	CO_NOT = 22,
	CO_SHL = 23,
	CO_SHR = 24,
	CO_NEGATE = 25,
	CO_TOTAL = 26,
};

int CalcOpPrecedence[CO_TOTAL] =
{
	0,
	0,
	0,
	9,    // add
	9,    // subtract
	10,   // multiply
	10,   // divide
	10,   // integer divide
	2,    // logical and
	5,    // bitwise and
	1,    // logical or
	3,    // bitwise or
	4,    // bitwise xor
	6,    // equal
	6,    // not equal
	7,    // greater
	7,    // not greater
	7,    // less
	7,    // not less
	10,   // modulus
	11,   // power
	12,   // logical not
	12,   // bitwise not
	8,    // shl
	8,    // shr
	12,   // negate
};

struct CalcOp
{
	eCalcOp Op;
	double Value;
};

BOOL EvaluateRPN(CalcOp* pList, int Size, double& Result)
{
	if (!Size)
		return 0;

	std::unique_ptr<double[]> stackPtr = std::make_unique<double[]>(Size / 2 + 2);
	double* pStack = stackPtr.get();

	int nStack = 0;

#define StackEmpty()           (nStack==0)
#define StackTop()             (pStack[nStack])
#define StackSetTop(do_assign) {pStack[nStack]##do_assign;}
#define StackPush(val)         {nStack++;pStack[nStack]=val;}
#define StackPop()             {if (!nStack) {FatalError("Illegal arithmetic in calculation"); return 0;}; nStack--;}

#define BinaryIntOp(op)        {int RightSide=(int)StackTop();StackPop();StackSetTop(=(double)(((int)StackTop())##op##RightSide));}
#define BinaryOp(op)           {double RightSide=StackTop();StackPop();StackSetTop(=StackTop()##op##RightSide);}
#define BinaryAssign(op)       {double RightSide=StackTop();StackPop();StackSetTop(##op##=RightSide);}

#define UnaryIntOp(op)         {StackSetTop(=op##((int)StackTop()));}
#define UnaryOp(op)            {StackSetTop(=op##(StackTop()));}

	for (int i = 0; i < Size; i++)
	{
		switch (pList[i].Op)
		{
		case CO_NUMBER:
			StackPush(pList[i].Value);
			break;
		case CO_ADD:
			BinaryAssign(+);
			break;
		case CO_MULTIPLY:
			BinaryAssign(*);
			break;
		case CO_SUBTRACT:
			BinaryAssign(-);
			break;
		case CO_NEGATE:
			UnaryOp(-);
			break;
		case CO_DIVIDE:
			if (StackTop())
			{
				BinaryAssign(/ );
			}
			else
			{
				//printf("Divide by zero error\n");
				FatalError("Divide by zero in calculation");
				return false;
			}
			break;

		case CO_IDIVIDE://TODO: SPECIAL HANDLING
		{
			int Right = (int)StackTop();
			if (Right)
			{
				StackPop();
				int Left = (int)StackTop();
				Left /= Right;
				StackSetTop(= Left);
			}
			else
			{
				//printf("Integer divide by zero error\n");
				FatalError("Divide by zero in calculation");
				return false;
			}
		}
		break;

		case CO_MODULUS://TODO: SPECIAL HANDLING
		{
			int Right = (int)StackTop();
			if (Right)
			{
				StackPop();
				int Left = (int)StackTop();
				Left %= Right;
				StackSetTop(= Left);
			}
			else
			{
				//printf("Modulus by zero error\n");
				FatalError("Modulus by zero in calculation");
				return false;
			}
		}
		break;

		case CO_LAND:
			BinaryOp(&&);
			break;
		case CO_LOR:
			BinaryOp(|| );
			break;
		case CO_EQUAL:
			BinaryOp(== );
			break;
		case CO_NOTEQUAL:
			BinaryOp(!= );
			break;
		case CO_GREATER:
			BinaryOp(> );
			break;
		case CO_NOTGREATER:
			BinaryOp(<= );
			break;
		case CO_LESS:
			BinaryOp(< );
			break;
		case CO_NOTLESS:
			BinaryOp(>= );
			break;
		case CO_SHL:
			BinaryIntOp(<< );
			break;
		case CO_SHR:
			BinaryIntOp(>> );
			break;
		case CO_AND:
			BinaryIntOp(&);
			break;
		case CO_OR:
			BinaryIntOp(| );
			break;
		case CO_XOR:
			BinaryIntOp(^);
			break;
		case CO_LNOT:
			UnaryIntOp(!);
			break;
		case CO_NOT:
			UnaryIntOp(~);
			break;
		case CO_POWER:
		{
			double RightSide = StackTop();
			StackPop();
			StackSetTop(= pow(StackTop(), RightSide));
		}
		break;
		}
	}

	Result = StackTop();

#undef StackEmpty
#undef StackTop
#undef StackPush
#undef StackPop

	delete[] pStack;
	return true;
}

bool FastCalculate(char* szFormula, double& Result)
{
	//DebugSpew("FastCalculate(%s)",szFormula);
	if (!szFormula || !szFormula[0])
		return false;

	int Length = (int)strlen(szFormula);
	int MaxOps = (Length + 1);

	std::unique_ptr<CalcOp[]> OpsList = std::make_unique<CalcOp[]>(MaxOps);
	CalcOp* pOpList = OpsList.get();
	memset(pOpList, 0, sizeof(CalcOp) * MaxOps);

	std::unique_ptr<eCalcOp[]> Stack = std::make_unique<eCalcOp[]>(MaxOps);
	eCalcOp* pStack = Stack.get();
	memset(pStack, 0, sizeof(eCalcOp) * MaxOps);

	int nOps = 0;
	int nStack = 0;
	char* pEnd = szFormula + Length;
	char CurrentToken[MAX_STRING] = { 0 };
	char* pToken = &CurrentToken[0];

#define OpToList(op)         { pOpList[nOps].Op = op; nOps++; }
#define ValueToList(val)     { pOpList[nOps].Value = val; nOps++; }
#define StackEmpty()         (nStack == 0)
#define StackTop()           (pStack[nStack])
#define StackPush(op)        { nStack++; pStack[nStack] = op; }
#define StackPop()           { if (!nStack) { FatalError("Illegal arithmetic in calculation"); return 0; } nStack--;}
#define HasPrecedence(a,b)   ( CalcOpPrecedence[a] >= CalcOpPrecedence[b])
#define MoveStack(op) {                                                                        \
	while (!StackEmpty() && StackTop() != CO_OPENPARENS && HasPrecedence(StackTop(), op)) {    \
		OpToList(StackTop());                                                                  \
		StackPop();                                                                            \
	}                                                                                          \
}
#define FinishString()       { if (pToken != &CurrentToken[0]) { *pToken = 0; ValueToList(atof(CurrentToken)); pToken = &CurrentToken[0]; *pToken=0; }}
#define NewOp(op)            { FinishString(); MoveStack(op); StackPush(op); }
#define NextChar(ch)         { *pToken = ch; pToken++; }

	bool WasParen = false;
	for (char* pCur = szFormula; pCur < pEnd; pCur++)
	{
		switch (*pCur)
		{
		case ' ':
			continue;
		case '(':
			FinishString();
			StackPush(CO_OPENPARENS);
			break;
		case ')':
			FinishString();
			while (StackTop() != CO_OPENPARENS)
			{
				OpToList(StackTop());
				StackPop();
			}
			StackPop();
			WasParen = true;
			continue;
		case '+':
			if (pCur[1] != '+')
				NewOp(CO_ADD);
			break;
		case '-':
			if (pCur[1] == '-')
			{
				pCur++;
				NewOp(CO_ADD);
			}
			else
			{
				if (CurrentToken[0] || WasParen)
				{
					NewOp(CO_SUBTRACT);
				}
				else
					NewOp(CO_NEGATE);
			}
			break;
		case '*':
			NewOp(CO_MULTIPLY);
			break;
		case '\\':
			NewOp(CO_IDIVIDE);
			break;
		case '/':
			NewOp(CO_DIVIDE);
			break;
		case '|':
			if (pCur[1] == '|')
			{
				// Logical OR
				++pCur;
				NewOp(CO_LOR);
			}
			else
			{
				// Bitwise OR
				NewOp(CO_OR);
			}
			break;
		case '%':
			NewOp(CO_MODULUS);
			break;
		case '~':
			NewOp(CO_NOT);
			break;
		case '&':
			if (pCur[1] == '&')
			{
				// Logical AND
				++pCur;
				NewOp(CO_LAND);
			}
			else
			{
				// Bitwise AND
				NewOp(CO_AND);
			}
			break;
		case '^':
			if (pCur[1] == '^')
			{
				// XOR
				++pCur;
				NewOp(CO_XOR);
			}
			else
			{
				// POWER
				NewOp(CO_POWER);
			}
			break;
		case '!':
			if (pCur[1] == '=')
			{
				++pCur;
				NewOp(CO_NOTEQUAL);
			}
			else
			{
				NewOp(CO_LNOT);
			}
			break;
		case '=':
			if (pCur[1] == '=')
			{
				++pCur;
				NewOp(CO_EQUAL);
			}
			else
			{
				//printf("Unparsable: '%c'\n",*pCur);
				// error
				return false;
			}
			break;
		case '<':
			if (pCur[1] == '=')
			{
				++pCur;
				NewOp(CO_NOTGREATER);
			}
			else if (pCur[1] == '<')
			{
				++pCur;
				NewOp(CO_SHL);
			}
			else
			{
				NewOp(CO_LESS);
			}
			break;
		case '>':
			if (pCur[1] == '=')
			{
				++pCur;
				NewOp(CO_NOTLESS);
			}
			else if (pCur[1] == '>')
			{
				++pCur;
				NewOp(CO_SHR);
			}
			else
			{
				NewOp(CO_GREATER);
			}
			break;
		case '.':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '0':
			NextChar(*pCur);
			break;
		default:
		{
			//printf("Unparsable: '%c'\n",*pCur);
			FatalError("Unparsable in Calculation: '%c'", *pCur);
			// unparsable
			return false;
		}
		break;
		}
		WasParen = false;
	}
	FinishString();

	while (!StackEmpty())
	{
		OpToList(StackTop());
		StackPop();
	}

	return EvaluateRPN(pOpList, nOps, Result);
}

bool Calculate(const char* szFormula, double& Result)
{
	char Buffer[MAX_STRING] = { 0 };
	strcpy_s(Buffer, szFormula);
	_strupr_s(Buffer);

	while (char* pNull = strstr(Buffer, "NULL"))
	{
		pNull[0] = '0';
		pNull[1] = '.';
		pNull[2] = '0';
		pNull[3] = '0';
	}

	while (char* pTrue = strstr(Buffer, "TRUE"))
	{
		pTrue[0] = '1';
		pTrue[1] = '.';
		pTrue[2] = '0';
		pTrue[3] = '0';
	}

	while (char* pFalse = strstr(Buffer, "FALSE"))
	{
		pFalse[0] = '0';
		pFalse[1] = '.';
		pFalse[2] = '0';
		pFalse[3] = '0';
		pFalse[4] = '0';
	}

	bool Ret;
	Benchmark(bmCalculate, Ret = FastCalculate(Buffer, Result));
	return Ret;
}

bool PlayerHasAAAbility(DWORD AAIndex)
{
	for (int i = 0; i < AA_CHAR_MAX_REAL; i++)
	{
		if (pPCData->GetAlternateAbilityId(i) == AAIndex)
			return true;
	}
	return false;
}

#if 0
char* GetAANameByIndex(DWORD AAIndex)
{
	for (unsigned long nAbility = 0; nAbility < NUM_ALT_ABILITIES_ARRAY; nAbility++)
	{
		if (((PALTADVMGR)pAltAdvManager)->AltAbilities->AltAbilityList->Abilities[nAbility])
		{
			if (ALTABILITY * pAbility = ((PALTADVMGR)pAltAdvManager)->AltAbilities->AltAbilityList->Abilities[nAbility]->Ability)
			{
				if (pAbility->Index == AAIndex)
				{
					return pStringTable->getString(pAbility->nName, 0);
				}
			}
		}
	}
	return "AA Not Found";
}
#endif

int GetAAIndexByName(const char* AAName)
{
	int level = -1;
	if (PSPAWNINFO pMe = (PSPAWNINFO)pLocalPlayer)
	{
		level = pMe->Level;
	}

	// check bought aa's first
	for (int nAbility = 0; nAbility < AA_CHAR_MAX_REAL; nAbility++)
	{
		if (ALTABILITY * pAbility = GetAAByIdWrapper(pPCData->GetAlternateAbilityId(nAbility), level))
		{
			if (const char* pName = pCDBStr->GetString(pAbility->nName, eAltAbilityName))
			{
				if (!_stricmp(AAName, pName))
				{
					return pAbility->Index;
				}
			}
		}
	}

	// not found? fine lets check them all then...
	for (int nAbility = 0; nAbility < NUM_ALT_ABILITIES; nAbility++)
	{
		if (ALTABILITY * pAbility = GetAAByIdWrapper(nAbility, level))
		{
			if (const char* pName = pCDBStr->GetString(pAbility->nName, eAltAbilityName))
			{
				if (!_stricmp(AAName, pName))
				{
					return pAbility->Index;
				}
			}
		}
	}

	return 0;
}

int GetAAIndexByID(int ID)
{
	// check our bought aa's first
	for (int nAbility = 0; nAbility < AA_CHAR_MAX_REAL; nAbility++)
	{
		if (ALTABILITY * pAbility = GetAAByIdWrapper(pPCData->GetAlternateAbilityId(nAbility)))
		{
			if (pAbility->ID == ID)
			{
				return pAbility->Index;
			}
		}
	}

	// didnt find it? fine we go through them all then...
	for (unsigned long nAbility = 0; nAbility < NUM_ALT_ABILITIES; nAbility++)
	{
		if (ALTABILITY * pAbility = GetAAByIdWrapper(nAbility))
		{
			if (pAbility->ID == ID)
			{
				return pAbility->Index;
			}
		}
	}

	return 0;
}

BOOL IsPCNear(PSPAWNINFO pSpawn, float Radius)
{
	PSPAWNINFO pClose = NULL;
	if (ppSpawnManager && pSpawnList)
	{
		pClose = (PSPAWNINFO)pSpawnList;
	}
	while (pClose)
	{
		if (!IsInGroup(pClose) && (pClose->Type == SPAWN_PLAYER))
		{
			if ((pClose != pSpawn) && (Distance3DToSpawn(pClose, pSpawn) < Radius))
				return TRUE;
		}
		pClose = pClose->pNext;
	}
	return false;
}

bool IsInGroup(SPAWNINFO* pSpawn, bool bCorpse)
{
	CHARINFO* pChar = GetCharInfo();
	if (!pChar->pGroupInfo)
		return false;
	if (pSpawn == pChar->pSpawn)
		return true;

	for (int i = 1; i < 6; i++)
	{
		GROUPMEMBER* pMember = pChar->pGroupInfo->pMember[i];

		if (pMember)
		{
			if (!bCorpse)
			{
				if (!_stricmp(pMember->Name.c_str(), pSpawn->Name))
				{
					return true;
				}
			}
			else
			{
				char szSearch[256] = { 0 };
				strcpy_s(szSearch, pMember->Name.c_str());
				strcat_s(szSearch, "'s corpse");
				DWORD l = strlen(szSearch);
				if (!_strnicmp(pSpawn->Name, szSearch, l))
				{
					return true;
				}
			}
		}
	}

	return false;
}

EQLIB_API BOOL IsInRaid(PSPAWNINFO pSpawn, BOOL bCorpse)
{
	DWORD i;
	if (pSpawn == GetCharInfo()->pSpawn)
		return TRUE;
	DWORD l = strlen(pSpawn->Name);
	for (i = 0; i < 72; i++)
	{
		if (!bCorpse) {
			if (!_strnicmp(pRaid->RaidMember[i].Name, pSpawn->Name, l + 1) && pRaid->RaidMember[i].nClass == pSpawn->mActorClient.Class) {
				return TRUE;
			}
		}
		else {
			char szSearch[256] = { 0 };
			strcpy_s(szSearch, pRaid->RaidMember[i].Name);
			strcat_s(szSearch, "'s corpse");
			l = strlen(szSearch);
			if (!_strnicmp(szSearch, pSpawn->Name, l) && pRaid->RaidMember[i].nClass == pSpawn->mActorClient.Class) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

BOOL IsInFellowship(PSPAWNINFO pSpawn, BOOL bCorpse)
{
	if (PCHARINFO pChar = GetCharInfo()) {
		if (!pChar->pSpawn)
			return FALSE;
		FELLOWSHIPINFO Fellowship = (FELLOWSHIPINFO)pChar->pSpawn->Fellowship;
		for (int i = 0; i < Fellowship.Members; i++)
		{
			if (!bCorpse) {
				if (!_stricmp(Fellowship.FellowshipMember[i].Name, pSpawn->Name))
				{
					return TRUE;
				}
			}
			else {
				char szSearch[256] = { 0 };
				strcpy_s(szSearch, Fellowship.FellowshipMember[i].Name);
				strcat_s(szSearch, "'s corpse");
				int l = strlen(szSearch);
				if (!_strnicmp(szSearch, pSpawn->Name, l) && Fellowship.FellowshipMember[i].Class == pSpawn->mActorClient.Class) {
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

BOOL IsNamed(PSPAWNINFO pSpawn)
{
	if (pSpawn) {
		char szTemp[MAX_STRING] = { 0 };

		if (GetSpawnType(pSpawn) != NPC)
			return false;
		if (!IsTargetable(pSpawn))
			return false;
		if (pSpawn->mActorClient.Class >= 20 && pSpawn->mActorClient.Class <= 35)  // NPC GMs
			return false;
		if (pSpawn->mActorClient.Class == 40)  // NPC bankers
			return false;
		if (pSpawn->mActorClient.Class == 41 || pSpawn->mActorClient.Class == 70)  // NPC/Quest/TBS merchants
			return false;
		if (pSpawn->mActorClient.Class == 60 || pSpawn->mActorClient.Class == 61)  //Ldon Merchants/Recruiters
			return false;
		if (pSpawn->mActorClient.Class == 62)  // Destructible Objects
			return false;
		if (pSpawn->mActorClient.Class == 63 || pSpawn->mActorClient.Class == 64 || pSpawn->mActorClient.Class == 74)  // Tribute Master/Guild Tribute Master/Personal Tribute Master
			return false;
		if (pSpawn->mActorClient.Class == 66)  // Guild Banker
			return false;
		if (pSpawn->mActorClient.Class == 67 || pSpawn->mActorClient.Class == 68)  //Don Merchants (Norrath's Keepers/Dark Reign)
			return false;
		if (pSpawn->mActorClient.Class == 69)  // Fellowship Registrar
			return false;
		if (pSpawn->mActorClient.Class == 71)  // Mercenary Liason
			return false;

		strcpy_s(szTemp, pSpawn->Name);
		char* Next_Token1 = 0;
		if (char* Cmd = strtok_s(szTemp, " ", &Next_Token1)) {

			// Checking for mobs that have 'A' or 'An' as their first name
			if (Cmd[0] == 'A')
			{
				if (Cmd[1] == '_')
					return false;
				else if (Cmd[1] == 'n')
					if (Cmd[2] == '_')
						return false;
			}
			if (!gUseNewNamedTest) {
				if ((!_strnicmp(Cmd, "Guard", 5)) ||
					(!_strnicmp(Cmd, "Defender", 8)) ||
					(!_strnicmp(Cmd, "Soulbinder", 10)) ||
					(!_strnicmp(Cmd, "Aura", 4)) ||
					(!_strnicmp(Cmd, "Sage", 4)) ||
					//(!_strnicmp(szTemp,"High_Priest",11))   ||
					(!_strnicmp(Cmd, "Ward", 4)) ||
					//(!_strnicmp(szTemp,"Shroudkeeper",12))  ||
					(!_strnicmp(Cmd, "Eye of", 6)) ||
					(!_strnicmp(Cmd, "Imperial_Crypt", 14)) ||
					(!_strnicmp(Cmd, "Diaku", 5)))
					return false;
			}
			if (Cmd[0] == '#' && (!gUseNewNamedTest || (gUseNewNamedTest && !pSpawn->Lastname[0])))
				return true;
			if (isupper(Cmd[0]) && (!gUseNewNamedTest || (gUseNewNamedTest && !pSpawn->Lastname[0])))
				return true;
		}
	}
	return false;
}

char* FormatSearchSpawn(char* Buffer, size_t BufferSize, MQSpawnSearch* pSearchSpawn)
{
	if (!Buffer)
		return nullptr;

	char szTemp[MAX_STRING] = { 0 };

	if (!pSearchSpawn)
	{
		strcpy_s(Buffer, BufferSize, "None");
		return Buffer;
	}

	const char* pszSpawnType = nullptr;
	switch (pSearchSpawn->SpawnType)
	{
	case NONE:
	default:
		pszSpawnType = "any";
		break;
	case PC:
		pszSpawnType = "pc";
		break;
	case MOUNT:
		pszSpawnType = "mount";
		break;
	case PET:
		pszSpawnType = "pet";
		break;
	case PCPET:
		pszSpawnType = "pcpet";
		break;
	case NPCPET:
		pszSpawnType = "npcpet";
		break;
	case XTARHATER:
		pszSpawnType = "xtarhater";
		break;
	case NPC:
		pszSpawnType = "npc";
		break;
	case CORPSE:
		pszSpawnType = "corpse";
		break;
	case TRIGGER:
		pszSpawnType = "trigger";
		break;
	case TRAP:
		pszSpawnType = "trap";
		break;
	case CHEST:
		pszSpawnType = "chest";
		break;
	case TIMER:
		pszSpawnType = "timer";
		break;
	case UNTARGETABLE:
		pszSpawnType = "untargetable";
		break;
	case MERCENARY:
		pszSpawnType = "mercenary";
		break;
	case FLYER:
		pszSpawnType = "flyer";
		break;
	}

	sprintf_s(Buffer, BufferSize, "(%d-%d) %s", pSearchSpawn->MinLevel, pSearchSpawn->MaxLevel, pszSpawnType);

	if (pSearchSpawn->szName[0] != 0)
	{
		if (pSearchSpawn->bExactName)
		{
			sprintf_s(szTemp, " whose name exactly matches %s", pSearchSpawn->szName);
		}
		else
		{
			sprintf_s(szTemp, " whose name contains %s", pSearchSpawn->szName);
		}
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->szRace[0] != 0)
	{
		sprintf_s(szTemp, " Race:%s", pSearchSpawn->szRace);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->szClass[0] != 0)
	{
		sprintf_s(szTemp, " Class:%s", pSearchSpawn->szClass);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->szBodyType[0] != 0)
	{
		sprintf_s(szTemp, " Body:%s", pSearchSpawn->szBodyType);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->GuildID != -1 && pSearchSpawn->GuildID != 0)
	{
		char* szGuild = GetGuildByID(pSearchSpawn->GuildID);
		sprintf_s(szTemp, " Guild:%s", szGuild ? szGuild : "Unknown");
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->bKnownLocation)
	{
		sprintf_s(szTemp, " at %1.2f,%1.2f", pSearchSpawn->yLoc, pSearchSpawn->xLoc);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->FRadius < 10000.0f)
	{
		sprintf_s(szTemp, " Radius:%1.2f", pSearchSpawn->FRadius);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->ZRadius < 10000.0f)
	{
		sprintf_s(szTemp, " Z:%1.2f", pSearchSpawn->ZRadius);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->Radius > 0.0f)
	{
		sprintf_s(szTemp, " NoPC:%1.2f", pSearchSpawn->Radius);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->SpawnID)
	{
		sprintf_s(szTemp, " ID:%d", pSearchSpawn->SpawnID);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->NotID)
	{
		sprintf_s(szTemp, " NotID:%d", pSearchSpawn->NotID);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->bAlert)
	{
		sprintf_s(szTemp, " Alert:%d", pSearchSpawn->AlertList);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->bNoAlert)
	{
		sprintf_s(szTemp, " NoAlert:%d", pSearchSpawn->NoAlertList);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->bNearAlert)
	{
		sprintf_s(szTemp, " NearAlert:%d", pSearchSpawn->NearAlertList);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->bNotNearAlert)
	{
		sprintf_s(szTemp, " NotNearAlert:%d", pSearchSpawn->NotNearAlertList);
		strcat_s(Buffer, BufferSize, szTemp);
	}

	if (pSearchSpawn->bGM && pSearchSpawn->SpawnType != NPC)
		strcat_s(Buffer, BufferSize, " GM");
	if (pSearchSpawn->bTrader)
		strcat_s(Buffer, BufferSize, " Trader");
	if (pSearchSpawn->bXTarHater)
		strcat_s(Buffer, BufferSize, " XTarHater");
	if (pSearchSpawn->bLFG)
		strcat_s(Buffer, BufferSize, " LFG");

	if (pSearchSpawn->bLight)
	{
		strcat_s(Buffer, BufferSize, " Light");
		if (pSearchSpawn->szLight[0])
		{
			strcat_s(Buffer, BufferSize, ":");
			strcat_s(Buffer, BufferSize, pSearchSpawn->szLight);
		}
	}

	if (pSearchSpawn->bLoS)
		strcat_s(Buffer, BufferSize, " LoS");

	return Buffer;
}

SPAWNINFO* NthNearestSpawn(MQSpawnSearch* pSearchSpawn, int Nth, SPAWNINFO* pOrigin, bool IncludeOrigin)
{
	if (!pSearchSpawn || !Nth || !pOrigin)
		return nullptr;

	std::vector<std::unique_ptr<MQRank>> spawnSet;
	SPAWNINFO* pSpawn = (SPAWNINFO*)pSpawnList;

	while (pSpawn)
	{
		if (IncludeOrigin || pSpawn != pOrigin)
		{
			if (SpawnMatchesSearch(pSearchSpawn, pOrigin, pSpawn))
			{
				// matches search, add to our set
				auto pNewRank = std::make_unique<MQRank>();
				pNewRank->VarPtr.Ptr = pSpawn;
				pNewRank->Value.Float = GetDistance3D(pOrigin->X, pOrigin->Y, pOrigin->Z, pSpawn->X, pSpawn->Y, pSpawn->Z);

				spawnSet.push_back(std::move(pNewRank));
			}
		}

		pSpawn = pSpawn->pNext;
	}

	if (Nth > static_cast<int>(spawnSet.size()))
	{
		return nullptr;
	}

	// sort our list
	std::sort(std::begin(spawnSet), std::end(spawnSet),
		[](const auto& a, const auto& b) { return a->Value.Float < b->Value.Float; });

	// get our Nth nearest
	return (SPAWNINFO*)spawnSet[Nth - 1]->VarPtr.Ptr;
}

int CountMatchingSpawns(MQSpawnSearch* pSearchSpawn, SPAWNINFO* pOrigin, bool IncludeOrigin)
{
	if (!pSearchSpawn || !pOrigin)
		return 0;

	int TotalMatching = 0;
	SPAWNINFO* pSpawn = (SPAWNINFO*)pSpawnList;

	if (IncludeOrigin)
	{
		while (pSpawn)
		{
			if (SpawnMatchesSearch(pSearchSpawn, pOrigin, pSpawn))
			{
				TotalMatching++;
			}
			pSpawn = pSpawn->pNext;
		}
	}
	else
	{
		while (pSpawn)
		{
			if (pSpawn != pOrigin && SpawnMatchesSearch(pSearchSpawn, pOrigin, pSpawn))
			{
				// matches search, add to our set
				TotalMatching++;
			}
			pSpawn = pSpawn->pNext;
		}
	}
	return TotalMatching;
}


SPAWNINFO* SearchThroughSpawns(MQSpawnSearch* pSearchSpawn, SPAWNINFO* pChar)
{
	SPAWNINFO* pFromSpawn = nullptr;

	if (pSearchSpawn->FromSpawnID > 0 && (pSearchSpawn->bTargNext || pSearchSpawn->bTargPrev))
	{
		pFromSpawn = (SPAWNINFO*)GetSpawnByID(pSearchSpawn->FromSpawnID);
		if (!pFromSpawn) return nullptr;
		for (int N = 0; N < 3000; N++)
		{
			if (EQP_DistArray[N].VarPtr.Ptr == pFromSpawn)
			{
				if (pSearchSpawn->bTargPrev)
				{
					N--;
					for (N; N >= 0; N--)
					{
						if (EQP_DistArray[N].VarPtr.Ptr &&
							SpawnMatchesSearch(pSearchSpawn, pFromSpawn, (PSPAWNINFO)EQP_DistArray[N].VarPtr.Ptr))
							return (PSPAWNINFO)EQP_DistArray[N].VarPtr.Ptr;
					}
				}
				else
				{
					N++;
					for (N; N < 3000; N++)
					{
						if (EQP_DistArray[N].VarPtr.Ptr &&
							SpawnMatchesSearch(pSearchSpawn, pFromSpawn, (PSPAWNINFO)EQP_DistArray[N].VarPtr.Ptr))
							return (PSPAWNINFO)EQP_DistArray[N].VarPtr.Ptr;
					}
				}
				return NULL;
			}
		}
	}
	return NthNearestSpawn(pSearchSpawn, 1, pChar, TRUE);
}

bool SearchSpawnMatchesSearchSpawn(MQSpawnSearch* pSearchSpawn1, MQSpawnSearch* pSearchSpawn2)
{
	if (pSearchSpawn1->AlertList != pSearchSpawn2->AlertList)
		return false;
	if (pSearchSpawn1->SpawnType != pSearchSpawn2->SpawnType)
		return false;
	if (pSearchSpawn1->FRadius != pSearchSpawn2->FRadius)
		return false;
	if (pSearchSpawn1->FromSpawnID != pSearchSpawn2->FromSpawnID)
		return false;
	if (pSearchSpawn1->GuildID != pSearchSpawn2->GuildID)
		return false;
	if (pSearchSpawn1->MaxLevel != pSearchSpawn2->MaxLevel)
		return false;
	if (pSearchSpawn1->MinLevel != pSearchSpawn2->MinLevel)
		return false;
	if (pSearchSpawn1->NearAlertList != pSearchSpawn2->NearAlertList)
		return false;
	if (pSearchSpawn1->NoAlertList != pSearchSpawn2->NoAlertList)
		return false;
	if (pSearchSpawn1->NotID != pSearchSpawn2->NotID)
		return false;
	if (pSearchSpawn1->NotNearAlertList != pSearchSpawn2->NotNearAlertList)
		return false;
	if (pSearchSpawn1->Radius != pSearchSpawn2->Radius)
		return false;
	if (pSearchSpawn1->SortBy != pSearchSpawn2->SortBy)
		return false;
	if (pSearchSpawn1->SpawnID != pSearchSpawn2->SpawnID)
		return false;
	if (_stricmp(pSearchSpawn1->szBodyType, pSearchSpawn2->szBodyType))
		return false;
	if (_stricmp(pSearchSpawn1->szClass, pSearchSpawn2->szClass))
		return false;
	if (_stricmp(pSearchSpawn1->szLight, pSearchSpawn2->szLight))
		return false;
	if (_stricmp(pSearchSpawn1->szName, pSearchSpawn2->szName))
		return false;
	if (_stricmp(pSearchSpawn1->szRace, pSearchSpawn2->szRace))
		return false;
	if (pSearchSpawn1->xLoc != pSearchSpawn2->xLoc)
		return false;
	if (pSearchSpawn1->yLoc != pSearchSpawn2->yLoc)
		return false;
	if (pSearchSpawn1->ZRadius != pSearchSpawn2->ZRadius)
		return false;
	if (pSearchSpawn1->bAlert != pSearchSpawn2->bAlert)
		return false;
	if (pSearchSpawn1->bAura != pSearchSpawn2->bAura)
		return false;
	if (pSearchSpawn1->bBanner != pSearchSpawn2->bBanner)
		return false;
	if (pSearchSpawn1->bCampfire != pSearchSpawn2->bCampfire)
		return false;
	if (pSearchSpawn1->bDps != pSearchSpawn2->bDps)
		return false;
	if (pSearchSpawn1->bExactName != pSearchSpawn2->bExactName)
		return false;
	if (pSearchSpawn1->bGM != pSearchSpawn2->bGM)
		return false;
	if (pSearchSpawn1->bGroup != pSearchSpawn2->bGroup)
		return false;
	if (pSearchSpawn1->bFellowship != pSearchSpawn2->bFellowship)
		return false;
	if (pSearchSpawn1->bKnight != pSearchSpawn2->bKnight)
		return false;
	if (pSearchSpawn1->bKnownLocation != pSearchSpawn2->bKnownLocation)
		return false;
	if (pSearchSpawn1->bLFG != pSearchSpawn2->bLFG)
		return false;
	if (pSearchSpawn1->bLight != pSearchSpawn2->bLight)
		return false;
	if (pSearchSpawn1->bLoS != pSearchSpawn2->bLoS)
		return false;
	if (pSearchSpawn1->bMerchant != pSearchSpawn2->bMerchant)
		return false;
	if (pSearchSpawn1->bBanker != pSearchSpawn2->bBanker)
		return false;
	if (pSearchSpawn1->bNamed != pSearchSpawn2->bNamed)
		return false;
	if (pSearchSpawn1->bNearAlert != pSearchSpawn2->bNearAlert)
		return false;
	if (pSearchSpawn1->bNoAlert != pSearchSpawn2->bNoAlert)
		return false;
	if (pSearchSpawn1->bNoGroup != pSearchSpawn2->bNoGroup)
		return false;
	if (pSearchSpawn1->bNoGuild != pSearchSpawn2->bNoGuild)
		return false;
	if (pSearchSpawn1->bNoPet != pSearchSpawn2->bNoPet)
		return false;
	if (pSearchSpawn1->bNotNearAlert != pSearchSpawn2->bNotNearAlert)
		return false;
	if (pSearchSpawn1->bRaid != pSearchSpawn2->bRaid)
		return false;
	if (pSearchSpawn1->bSlower != pSearchSpawn2->bSlower)
		return false;
	if (pSearchSpawn1->bSpawnID != pSearchSpawn2->bSpawnID)
		return false;
	if (pSearchSpawn1->bTank != pSearchSpawn2->bTank)
		return false;
	if (pSearchSpawn1->bTargetable != pSearchSpawn2->bTargetable)
		return false;
	if (pSearchSpawn1->bTargNext != pSearchSpawn2->bTargNext)
		return false;
	if (pSearchSpawn1->bTargPrev != pSearchSpawn2->bTargPrev)
		return false;
	if (pSearchSpawn1->bTrader != pSearchSpawn2->bTrader)
		return false;
	if (pSearchSpawn1->bTributeMaster != pSearchSpawn2->bTributeMaster)
		return false;
	if (pSearchSpawn1->bXTarHater != pSearchSpawn2->bXTarHater)
		return false;

	return true;
}

bool SpawnMatchesSearch(MQSpawnSearch* pSearchSpawn, SPAWNINFO* pChar, SPAWNINFO* pSpawn)
{
	eSpawnType SpawnType = GetSpawnType(pSpawn);

	if (SpawnType == PET && (pSearchSpawn->SpawnType == PCPET || pSearchSpawn->SpawnType == NPCPET))
	{
		if (SPAWNINFO * pTheMaster = (SPAWNINFO*)GetSpawnByID(pSpawn->MasterID))
		{
			if (pTheMaster->Type == SPAWN_NPC)
			{
				SpawnType = NPCPET;
			}
			else if (pTheMaster->Type == SPAWN_PLAYER)
			{
				SpawnType = PCPET;
			}
		}
	}

	if (pSearchSpawn->SpawnType != SpawnType && pSearchSpawn->SpawnType != NONE)
	{
		if (pSearchSpawn->SpawnType == NPCCORPSE)
		{
			if (SpawnType != CORPSE || pSpawn->Deity)
			{
				return false;
			}
		}
		else if (pSearchSpawn->SpawnType == PCCORPSE)
		{
			if (SpawnType != CORPSE || !pSpawn->Deity)
			{
				return false;
			}
		}
		else if (pSearchSpawn->SpawnType == NPC && SpawnType == UNTARGETABLE)
		{
			return FALSE;
		}
		else {

			// if the search type is not npc or the mob type is UNT, continue?
			// stupid /who


			if (pSearchSpawn->SpawnType != NPC || SpawnType != UNTARGETABLE)
				return FALSE;
		}
	}
	if (pSearchSpawn->MinLevel && pSpawn->Level < pSearchSpawn->MinLevel)
		return FALSE;
	if (pSearchSpawn->MaxLevel && pSpawn->Level > pSearchSpawn->MaxLevel)
		return FALSE;
	if (pSearchSpawn->NotID == pSpawn->SpawnID)
		return FALSE;
	if (pSearchSpawn->bSpawnID && pSearchSpawn->SpawnID != pSpawn->SpawnID)
		return FALSE;
	if (pSearchSpawn->GuildID != -1 && pSearchSpawn->GuildID != pSpawn->GuildID)
		return FALSE;
	if (pSearchSpawn->bGM && pSearchSpawn->SpawnType != NPC)
		if (!pSpawn->GM)
			return FALSE;
	if (pSearchSpawn->bGM && pSearchSpawn->SpawnType == NPC)
		if (pSpawn->mActorClient.Class < 20 || pSpawn->mActorClient.Class > 35)
			return FALSE;
	if (pSearchSpawn->bNamed && !IsNamed(pSpawn))
		return FALSE;
	if (pSearchSpawn->bMerchant && pSpawn->mActorClient.Class != 41)
		return FALSE;
	if (pSearchSpawn->bBanker && pSpawn->mActorClient.Class != 40)
		return FALSE;
	if (pSearchSpawn->bTributeMaster && pSpawn->mActorClient.Class != 63)
		return FALSE;
	if (pSearchSpawn->bNoGuild && (pSpawn->GuildID != -1 && pSpawn->GuildID != 0))
		return FALSE;
	if (pSearchSpawn->bKnight && pSearchSpawn->SpawnType != NPC)
		if (pSpawn->mActorClient.Class != 3 && pSpawn->mActorClient.Class != 5)
			return FALSE;
	if (pSearchSpawn->bTank && pSearchSpawn->SpawnType != NPC)
		if (pSpawn->mActorClient.Class != 3 && pSpawn->mActorClient.Class != 5 && pSpawn->mActorClient.Class != 1)
			return FALSE;
	if (pSearchSpawn->bHealer && pSearchSpawn->SpawnType != NPC)
		if (pSpawn->mActorClient.Class != 2 && pSpawn->mActorClient.Class != 6)
			return FALSE;
	if (pSearchSpawn->bDps && pSearchSpawn->SpawnType != NPC)
	{
		if (pSpawn->mActorClient.Class != 4 && pSpawn->mActorClient.Class != 9 && pSpawn->mActorClient.Class != 12)
			return false;
	}
	if (pSearchSpawn->bSlower && pSearchSpawn->SpawnType != NPC)
		if (pSpawn->mActorClient.Class != 10 && pSpawn->mActorClient.Class != 14 && pSpawn->mActorClient.Class != 15)
			return FALSE;
	if (pSearchSpawn->bLFG && !pSpawn->LFG)
		return FALSE;
	if (pSearchSpawn->bTrader && !pSpawn->Trader)
		return FALSE;
	if (pSearchSpawn->bXTarHater) {
		bool foundhater = 0;
		if (PCHARINFO pmyChar = GetCharInfo()) {
			if (ExtendedTargetList * xtm = pmyChar->pXTargetMgr)
			{
				if (xtm->XTargetSlots.Count) {
					for (int i = 0; i < pmyChar->pXTargetMgr->XTargetSlots.Count; i++) {
						XTARGETSLOT xts = xtm->XTargetSlots[i];
						if (xts.xTargetType == XTARGET_AUTO_HATER && xts.XTargetSlotStatus && xts.SpawnID) {
							if (PSPAWNINFO pxtarSpawn = (PSPAWNINFO)GetSpawnByID(xts.SpawnID)) {
								if (pxtarSpawn->SpawnID == pSpawn->SpawnID) {
									foundhater = true;
								}
							}
						}
					}
				}
			}
		}
		if (!foundhater) {
			return FALSE;
		}
	}


	if (pSearchSpawn->bGroup) {
		BOOL ingrp = 0;
		if (pSearchSpawn->SpawnType == PCCORPSE || pSpawn->Type == SPAWN_CORPSE) {
			ingrp = IsInGroup(pSpawn, 1);
		}
		else {
			ingrp = IsInGroup(pSpawn);
		}
		if (!ingrp)
			return FALSE;
	}
	if (pSearchSpawn->bFellowship) {
		BOOL infellowship = 0;
		if (pSearchSpawn->SpawnType == PCCORPSE || pSpawn->Type == SPAWN_CORPSE) {
			infellowship = IsInFellowship(pSpawn, 1);
		}
		else {
			infellowship = IsInFellowship(pSpawn);
		}
		if (!infellowship)
			return FALSE;
	}
	if (pSearchSpawn->bNoGroup && IsInGroup(pSpawn))
		return FALSE;
	if (pSearchSpawn->bRaid) {
		BOOL ingrp = 0;
		if (pSearchSpawn->SpawnType == PCCORPSE || pSpawn->Type == SPAWN_CORPSE) {
			ingrp = IsInRaid(pSpawn, 1);
		}
		else {
			ingrp = IsInRaid(pSpawn);
		}
		if (!ingrp)
			return FALSE;
	}
	if (pSearchSpawn->bKnownLocation)
	{
		if ((pSearchSpawn->xLoc != pSpawn->X || pSearchSpawn->yLoc != pSpawn->Y))
			if (pSearchSpawn->FRadius<10000.0f && Distance3DToPoint(pSpawn, pSearchSpawn->xLoc, pSearchSpawn->yLoc, pSearchSpawn->zLoc)>pSearchSpawn->FRadius)
				return FALSE;
	}
	else if (pSearchSpawn->FRadius<10000.0f && Distance3DToSpawn(pChar, pSpawn)>pSearchSpawn->FRadius)
		return FALSE;

	if (pSearchSpawn->Radius > 0.0f && IsPCNear(pSpawn, pSearchSpawn->Radius))
		return FALSE;
	if (gZFilter < 10000.0f && ((pSpawn->Z > pSearchSpawn->zLoc + gZFilter) || (pSpawn->Z < pSearchSpawn->zLoc - gZFilter)))
		return FALSE;
	if (pSearchSpawn->ZRadius < 10000.0f && (pSpawn->Z > pSearchSpawn->zLoc + pSearchSpawn->ZRadius || pSpawn->Z < pSearchSpawn->zLoc - pSearchSpawn->ZRadius))
		return FALSE;
	if (pSearchSpawn->bLight)
	{
		const char* pLight = GetLightForSpawn(pSpawn);
		if (!_stricmp(pLight, "NONE"))
			return FALSE;
		if (pSearchSpawn->szLight[0] && _stricmp(pLight, pSearchSpawn->szLight))
			return FALSE;
	}
	if ((pSearchSpawn->bAlert) && CAlerts.AlertExist(pSearchSpawn->AlertList)) {
		if (!IsAlert(pChar, pSpawn, pSearchSpawn->AlertList))
			return FALSE;
	}
	if ((pSearchSpawn->bNoAlert) && CAlerts.AlertExist(pSearchSpawn->NoAlertList)) {
		if (IsAlert(pChar, pSpawn, pSearchSpawn->NoAlertList))
			return FALSE;
	}
	if ((pSearchSpawn->bNotNearAlert) && (GetClosestAlert(pSpawn, pSearchSpawn->NotNearAlertList)))
		return FALSE;
	if ((pSearchSpawn->bNearAlert) && (!GetClosestAlert(pSpawn, pSearchSpawn->NearAlertList)))
		return FALSE;
	if (pSearchSpawn->szClass[0] && _stricmp(pSearchSpawn->szClass, GetClassDesc(pSpawn->mActorClient.Class)))
		return FALSE;
	if (pSearchSpawn->szBodyType[0] && _stricmp(pSearchSpawn->szBodyType, GetBodyTypeDesc(GetBodyType(pSpawn))))
		return FALSE;
	if (pSearchSpawn->szRace[0] && _stricmp(pSearchSpawn->szRace, pEverQuest->GetRaceDesc(pSpawn->mActorClient.Race)))
		return FALSE;
	//if (pSearchSpawn->bLoS && (!LineOfSight(pChar,pSpawn)))
	if (pSearchSpawn->bLoS && (!pCharSpawn->CanSee(*(PlayerClient*)pSpawn)))
		return FALSE;
	if (pSearchSpawn->bTargetable && (!IsTargetable(pSpawn)))
		return FALSE;
	if (pSearchSpawn->PlayerState && !(pSpawn->PlayerState & pSearchSpawn->PlayerState)) // if player state isn't 0 and we have that bit set
		return FALSE;
	if (pSearchSpawn->szName[0] && pSpawn->Name[0])
	{
		char szName[MAX_STRING] = { 0 };
		char szSearchName[MAX_STRING] = { 0 };
		strcpy_s(szName, pSpawn->Name);
		_strlwr_s(szName);
		strcpy_s(szSearchName, pSearchSpawn->szName);
		_strlwr_s(szSearchName);
		if (!strstr(szName, szSearchName) && !strstr(CleanupName(szName, sizeof(szName), FALSE), szSearchName))
			return FALSE;
		if (pSearchSpawn->bExactName && _stricmp(CleanupName(szName, sizeof(szName), FALSE, !gbExactSearchCleanNames), pSearchSpawn->szName))
			return FALSE;
	}
	return TRUE;
}

char* ParseSearchSpawnArgs(char* szArg, char* szRest, MQSpawnSearch* pSearchSpawn)
{
	if (szArg && pSearchSpawn) {
		if (!_stricmp(szArg, "pc")) {
			pSearchSpawn->SpawnType = PC;
		}
		else if (!_stricmp(szArg, "npc")) {
			pSearchSpawn->SpawnType = NPC;
		}
		else if (!_stricmp(szArg, "mount")) {
			pSearchSpawn->SpawnType = MOUNT;
		}
		else if (!_stricmp(szArg, "pet")) {
			pSearchSpawn->SpawnType = PET;
		}
		else if (!_stricmp(szArg, "pcpet")) {
			pSearchSpawn->SpawnType = PCPET;
		}
		else if (!_stricmp(szArg, "npcpet")) {
			pSearchSpawn->SpawnType = NPCPET;
		}
		else if (!_stricmp(szArg, "xtarhater")) {
			pSearchSpawn->bXTarHater = TRUE;
		}
		else if (!_stricmp(szArg, "nopet")) {
			pSearchSpawn->bNoPet = TRUE;
		}
		else if (!_stricmp(szArg, "corpse")) {
			pSearchSpawn->SpawnType = CORPSE;
		}
		else if (!_stricmp(szArg, "npccorpse")) {
			pSearchSpawn->SpawnType = NPCCORPSE;
		}
		else if (!_stricmp(szArg, "pccorpse")) {
			pSearchSpawn->SpawnType = PCCORPSE;
		}
		else if (!_stricmp(szArg, "trigger")) {
			pSearchSpawn->SpawnType = TRIGGER;
		}
		else if (!_stricmp(szArg, "untargetable")) {
			pSearchSpawn->SpawnType = UNTARGETABLE;
		}
		else if (!_stricmp(szArg, "trap")) {
			pSearchSpawn->SpawnType = TRAP;
		}
		else if (!_stricmp(szArg, "chest")) {
			pSearchSpawn->SpawnType = CHEST;
		}
		else if (!_stricmp(szArg, "timer")) {
			pSearchSpawn->SpawnType = TIMER;
		}
		else if (!_stricmp(szArg, "aura")) {
			pSearchSpawn->SpawnType = AURA;
		}
		else if (!_stricmp(szArg, "object")) {
			pSearchSpawn->SpawnType = OBJECT;
		}
		else if (!_stricmp(szArg, "banner")) {
			pSearchSpawn->SpawnType = BANNER;
		}
		else if (!_stricmp(szArg, "campfire")) {
			pSearchSpawn->SpawnType = CAMPFIRE;
		}
		else if (!_stricmp(szArg, "mercenary")) {
			pSearchSpawn->SpawnType = MERCENARY;
		}
		else if (!_stricmp(szArg, "flyer")) {
			pSearchSpawn->SpawnType = FLYER;
		}
		else if (!_stricmp(szArg, "any")) {
			pSearchSpawn->SpawnType = NONE;
		}
		else if (!_stricmp(szArg, "next")) {
			pSearchSpawn->bTargNext = TRUE;
		}
		else if (!_stricmp(szArg, "prev")) {
			pSearchSpawn->bTargPrev = TRUE;
		}
		else if (!_stricmp(szArg, "lfg")) {
			pSearchSpawn->bLFG = TRUE;
		}
		else if (!_stricmp(szArg, "gm")) {
			pSearchSpawn->bGM = TRUE;
		}
		else if (!_stricmp(szArg, "group")) {
			pSearchSpawn->bGroup = TRUE;
		}
		else if (!_stricmp(szArg, "fellowship")) {
			pSearchSpawn->bFellowship = TRUE;
		}
		else if (!_stricmp(szArg, "nogroup")) {
			pSearchSpawn->bNoGroup = TRUE;
		}
		else if (!_stricmp(szArg, "raid")) {
			pSearchSpawn->bRaid = TRUE;
		}
		else if (!_stricmp(szArg, "noguild")) {
			pSearchSpawn->bNoGuild = TRUE;
		}
		else if (!_stricmp(szArg, "trader")) {
			pSearchSpawn->bTrader = TRUE;
		}
		else if (!_stricmp(szArg, "named")) {
			pSearchSpawn->bNamed = TRUE;
		}
		else if (!_stricmp(szArg, "merchant")) {
			pSearchSpawn->bMerchant = TRUE;
		}
		else if (!_stricmp(szArg, "banker")) {
			pSearchSpawn->bBanker = TRUE;
		}
		else if (!_stricmp(szArg, "tribute")) {
			pSearchSpawn->bTributeMaster = TRUE;
		}
		else if (!_stricmp(szArg, "knight")) {
			pSearchSpawn->bKnight = TRUE;
		}
		else if (!_stricmp(szArg, "tank")) {
			pSearchSpawn->bTank = TRUE;
		}
		else if (!_stricmp(szArg, "healer")) {
			pSearchSpawn->bHealer = TRUE;
		}
		else if (!_stricmp(szArg, "dps")) {
			pSearchSpawn->bDps = TRUE;
		}
		else if (!_stricmp(szArg, "slower")) {
			pSearchSpawn->bSlower = TRUE;
		}
		else if (!_stricmp(szArg, "los")) {
			pSearchSpawn->bLoS = TRUE;
		}
		else if (!_stricmp(szArg, "targetable")) {
			pSearchSpawn->bTargetable = TRUE;
		}
		else if (!_stricmp(szArg, "range")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->MinLevel = atoi(szArg);
			GetArg(szArg, szRest, 2);
			pSearchSpawn->MaxLevel = atoi(szArg);
			szRest = GetNextArg(szRest, 2);
		}
		else if (!_stricmp(szArg, "loc")) {
			pSearchSpawn->bKnownLocation = TRUE;
			GetArg(szArg, szRest, 1);
			pSearchSpawn->xLoc = (float)atof(szArg);
			GetArg(szArg, szRest, 2);
			pSearchSpawn->yLoc = (float)atof(szArg);
			GetArg(szArg, szRest, 3);
			pSearchSpawn->zLoc = (float)atof(szArg);
			if (pSearchSpawn->zLoc == 0.0) {
				pSearchSpawn->zLoc = ((PSPAWNINFO)pCharSpawn)->Z;
				szRest = GetNextArg(szRest, 2);
			}
			else {
				szRest = GetNextArg(szRest, 3);
			}
		}
		else if (!_stricmp(szArg, "id")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->bSpawnID = TRUE;
			pSearchSpawn->SpawnID = atoi(szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "radius")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->FRadius = atof(szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "body")) {
			GetArg(szArg, szRest, 1);
			strcpy_s(pSearchSpawn->szBodyType, szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "class")) {
			GetArg(szArg, szRest, 1);
			strcpy_s(pSearchSpawn->szClass, szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "race")) {
			GetArg(szArg, szRest, 1);
			strcpy_s(pSearchSpawn->szRace, szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "light")) {
			DWORD Light = -1;
			DWORD i = 0;
			GetArg(szArg, szRest, 1);
			if (szArg[0] != 0) for (i = 0; i < LIGHT_COUNT; i++) if (!_stricmp(szLights[i], szArg)) Light = i;
			if (Light != -1) {
				strcpy_s(pSearchSpawn->szLight, szLights[Light]);
				szRest = GetNextArg(szRest, 1);
			}
			else {
				pSearchSpawn->szLight[0] = 0;
			}
			pSearchSpawn->bLight = TRUE;
		}
		else if (!_stricmp(szArg, "guild")) {
			pSearchSpawn->GuildID = GetCharInfo()->GuildID;
		}
		else if (!_stricmp(szArg, "guildname")) {
			int64_t GuildID = -1;
			GetArg(szArg, szRest, 1);
			if (szArg[0] != 0)
				GuildID = GetGuildIDByName(szArg);
			if (GuildID != -1 && GuildID != 0) {
				pSearchSpawn->GuildID = GuildID;
				szRest = GetNextArg(szRest, 1);
			}
		}
		else if (!_stricmp(szArg, "alert")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->AlertList = atoi(szArg);
			szRest = GetNextArg(szRest, 1);
			pSearchSpawn->bAlert = TRUE;
		}
		else if (!_stricmp(szArg, "noalert")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->NoAlertList = atoi(szArg);
			szRest = GetNextArg(szRest, 1);
			pSearchSpawn->bNoAlert = TRUE;
		}
		else if (!_stricmp(szArg, "notnearalert")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->NotNearAlertList = atoi(szArg);
			szRest = GetNextArg(szRest, 1);
			pSearchSpawn->bNotNearAlert = TRUE;
		}
		else if (!_stricmp(szArg, "nearalert")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->NearAlertList = atoi(szArg);
			szRest = GetNextArg(szRest, 1);
			pSearchSpawn->bNearAlert = TRUE;
		}
		else if (!_stricmp(szArg, "zradius")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->ZRadius = atof(szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "notid")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->NotID = atoi(szArg);
			szRest = GetNextArg(szRest, 1);
		}
		else if (!_stricmp(szArg, "nopcnear")) {
			GetArg(szArg, szRest, 1);
			if ((szArg[0] == 0) || (0.0f == (pSearchSpawn->Radius = (float)atof(szArg)))) {
				pSearchSpawn->Radius = 200.0f;
			}
			else {
				szRest = GetNextArg(szRest, 1);
			}
		}
		else if (!_stricmp(szArg, "playerstate")) {
			GetArg(szArg, szRest, 1);
			pSearchSpawn->PlayerState |= atoi(szArg); // This allows us to pass multiple playerstate args
			szRest = GetNextArg(szRest, 1);
		}
		else if (IsNumber(szArg))
		{
			pSearchSpawn->MinLevel = atoi(szArg);
			pSearchSpawn->MaxLevel = pSearchSpawn->MinLevel;
		}
		else
		{
			for (int index = 1; index < 17; index++)
			{
				if (!_stricmp(szArg, ClassInfo[index].Name) || !_stricmp(szArg, ClassInfo[index].ShortName))
				{
					strcpy_s(pSearchSpawn->szClass, pEverQuest->GetClassDesc(index));
					return szRest;
				}
			}

			if (pSearchSpawn->szName[0])
			{
				// multiple word name
				strcat_s(pSearchSpawn->szName, " ");
				strcat_s(pSearchSpawn->szName, szArg);
			}
			else
			{
				if (szArg[0] == '=')
				{
					pSearchSpawn->bExactName = true;
					szArg++;
				}
				strcpy_s(pSearchSpawn->szName, szArg);
			}
		}
	}

	return szRest;
}

void ParseSearchSpawn(const char* Buffer, MQSpawnSearch* pSearchSpawn)
{
	char szArg[MAX_STRING] = { 0 };
	char szMsg[MAX_STRING] = { 0 };
	char szLLine[MAX_STRING] = { 0 };
	char* szFilter = szLLine;
	BOOL DidTarget = FALSE;
	BOOL bArg = TRUE;

	bRunNextCommand = true;
	strcpy_s(szLLine, Buffer);
	_strlwr_s(szLLine);
	while (bArg) {
		GetArg(szArg, szFilter, 1);
		szFilter = GetNextArg(szFilter, 1);
		if (szArg[0] == 0) {
			bArg = FALSE;
		}
		else {
			szFilter = ParseSearchSpawnArgs(szArg, szFilter, pSearchSpawn);
		}
	}
}

bool GetClosestAlert(SPAWNINFO* pChar, uint32_t id)
{
	if (!ppSpawnManager)
		return false;
	if (!pSpawnList)
		return false;

	SPAWNINFO* pSpawn = nullptr;
	SPAWNINFO* pClosest = nullptr;

	float ClosestDistance = 50000.0f;

	char szName[MAX_STRING] = { 0 };

	std::vector<MQSpawnSearch> search;
	if (CAlerts.GetAlert(id, search))
	{
		for (auto& s : search)
		{
			if (pSpawn = SearchThroughSpawns(&s, pChar))
			{
				if (Distance3DToSpawn(pChar, pSpawn) < ClosestDistance)
				{
					pClosest = pSpawn;
				}
			}
		}
	}

	return pClosest != nullptr;
}

bool IsAlert(SPAWNINFO* pChar, SPAWNINFO* pSpawn, uint32_t id)
{
	char szName[MAX_STRING] = { 0 };

	MQSpawnSearch SearchSpawn;

	std::vector<MQSpawnSearch> alerts;
	if (CAlerts.GetAlert(id, alerts))
	{
		for (auto& search : alerts)
		{
			if (search.SpawnID > 0 && search.SpawnID != pSpawn->SpawnID)
				continue;

			// FIXME
			memcpy(&SearchSpawn, &search, sizeof(MQSpawnSearch));
			SearchSpawn.SpawnID = pSpawn->SpawnID;

			// if this spawn matches, it's true. This is an implied logical or
			if (SpawnMatchesSearch(&SearchSpawn, pChar, pSpawn))
				return true;
		}
	}

	return false;
}

// FIXME: This function is broken, and doesn't actually check against the CAlerts list.
bool CheckAlertForRecursion(MQSpawnSearch* pSearchSpawn, uint32_t id)
{
	if (gbIgnoreAlertRecursion)
		return false;

	if (!pSearchSpawn)
		return false;

	std::vector<MQSpawnSearch> ss;
	if (CAlerts.GetAlert(id, ss))
	{
		for (auto i = ss.begin(); i != ss.end(); i++)
		{
			if (pSearchSpawn->bAlert)
			{
				if (pSearchSpawn->AlertList == id)
				{
					return true;
				}

				if (CheckAlertForRecursion(pSearchSpawn, pSearchSpawn->AlertList))
				{
					return true;
				}
			}

			if (pSearchSpawn->bNoAlert)
			{
				if (pSearchSpawn->NoAlertList == id)
				{
					return true;
				}

				if (CheckAlertForRecursion(pSearchSpawn, pSearchSpawn->NoAlertList))
				{
					return true;
				}
			}

			if (pSearchSpawn->bNearAlert)
			{
				if (pSearchSpawn->NearAlertList == id)
				{
					return true;
				}

				if (CheckAlertForRecursion(pSearchSpawn, pSearchSpawn->NearAlertList))
				{
					return true;
				}
			}

			if (pSearchSpawn->bNotNearAlert)
			{
				if (pSearchSpawn->NotNearAlertList == id)
				{
					return true;
				}

				if (CheckAlertForRecursion(pSearchSpawn, pSearchSpawn->NotNearAlertList))
				{
					return true;
				}
			}
		}
	}

	return false;
}
// ***************************************************************************
// Function:    CleanupName
// Description: Cleans up NPC names
//              an_iksar_marauder23 = iksar marauder, an
// ***************************************************************************
char* CleanupName(char* szName, size_t BufferSize, bool Article, bool ForWhoList)
{
	char szTemp[MAX_STRING] = { 0 };
	int j = 0;

	for (size_t i = 0; i < strlen(szName); i++)
	{
		switch (szName[i])
		{
		case '_':
			szTemp[j++] = ' ';
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			break;
		case '#':
			if (!ForWhoList)
				break;
		default:
			szTemp[j++] = szName[i];
		}
	}

	strcpy_s(szName, BufferSize, szTemp);

	if (!Article) return szName;

	if (!_strnicmp(szName, "a ", 2))
	{
		sprintf_s(szTemp, "%s, a", szName + 2);
		strcpy_s(szName, BufferSize, szTemp);
	}
	else if (!_strnicmp(szName, "an ", 3))
	{
		sprintf_s(szTemp, "%s, an", szName + 3);
		strcpy_s(szName, BufferSize, szTemp);
	}
	else if (!_strnicmp(szName, "the ", 4))
	{
		sprintf_s(szTemp, "%s, the", szName + 4);
		strcpy_s(szName, BufferSize, szTemp);
	}

	return szName;
}

// ***************************************************************************
// Function:    SuperWhoDisplay
// Description: Displays our SuperWho / SuperWhoTarget
// ***************************************************************************
void SuperWhoDisplay(SPAWNINFO* pSpawn, DWORD Color)
{
	char szName[MAX_STRING] = { 0 };
	char szMsg[MAX_STRING] = { 0 };
	char szMsgL[MAX_STRING] = { 0 };
	char szTemp[MAX_STRING] = { 0 };
	//strcpy_s(szName,pSpawn->Name);
	strcpy_s(szName, pSpawn->DisplayedName);
	if (pSpawn->Type == SPAWN_PLAYER) {
		if (gFilterSWho.Lastname && strlen(pSpawn->Lastname) > 0) {
			strcat_s(szName, " ");
			strcat_s(szName, pSpawn->Lastname);
		}
		if (gFilterSWho.Guild && pSpawn->GuildID != -1 && pSpawn->GuildID != 0) {
			strcat_s(szName, " <");
			char* szGuild = GetGuildByID(pSpawn->GuildID);
			strcat_s(szName, szGuild ? szGuild : "Unknown Guild");
			strcat_s(szName, ">");
		}
	}
	else {
		//CleanupName(szName);
		if (gFilterSWho.Lastname && strlen(pSpawn->Lastname) > 0) {
			strcat_s(szName, " (");
			strcat_s(szName, pSpawn->Lastname);
			strcat_s(szName, ")");
		}
	}
	char GM[MAX_STRING] = { 0 };
	if (gFilterSWho.GM && pSpawn->GM) {
		if (pSpawn->Level >= 50) {
			strcpy_s(GM, "\ay*GM*\ax");
		}
		else if (pSpawn->Level == 20) {
			strcpy_s(GM, "\a-y*Guide Applicant*\ax");
		}
		else {
			strcpy_s(GM, "\a-y*Guide*\ax");
		}
	}
	szMsg[0] = '\a';
	szMsg[2] = 0;
	if (Color || gFilterSWho.ConColor)
	{
		switch (ConColor(pSpawn))
		{
		case CONCOLOR_WHITE:
			szMsg[1] = 'w';
			break;
		case CONCOLOR_YELLOW:
			szMsg[1] = 'y';
			break;
		case CONCOLOR_RED:
			szMsg[1] = 'r';
			break;
		case CONCOLOR_BLUE:
			szMsg[1] = 'u';
			break;
		case CONCOLOR_LIGHTBLUE:
			szMsg[1] = 't';
			break;
		case CONCOLOR_GREEN:
			szMsg[1] = 'g';
			break;
		case CONCOLOR_GREY:
			szMsg[1] = '-';
			szMsg[2] = 'w';
			break;
		default:
			szMsg[1] = 'm';
			break;
		}
	}
	else
	{
		szMsg[1] = 'w';
	}
	if (gFilterSWho.GM) strcat_s(szMsg, GM);
	if (gFilterSWho.Level || gFilterSWho.Race || gFilterSWho.Body || gFilterSWho.Class) {
		strcat_s(szMsg, "\a-u[\ax");
		if (gFilterSWho.Level) {
			_itoa_s(pSpawn->Level, szTemp, 10);
			strcat_s(szMsg, szTemp);
			strcat_s(szMsg, " ");
		}
		if (gFilterSWho.Race) {
			strcat_s(szMsg, pEverQuest->GetRaceDesc(pSpawn->mActorClient.Race));
			strcat_s(szMsg, " ");
		}
		if (gFilterSWho.Body) {
			strcat_s(szMsg, GetBodyTypeDesc(GetBodyType(pSpawn)));
			strcat_s(szMsg, " ");
		}
		if (gFilterSWho.Class) {
			strcat_s(szMsg, GetClassDesc(pSpawn->mActorClient.Class));
			strcat_s(szMsg, " ");
		}
		szMsg[strlen(szMsg) - 1] = 0;
		strcat_s(szMsg, "\a-u]\ax");
	}
	strcat_s(szMsg, " ");
	strcat_s(szMsg, szName);
	//strcat_s(szMsg,"\ax");

	if (pSpawn->Type == SPAWN_PLAYER) {
		if (gFilterSWho.Anon && pSpawn->Anon > 0) {
			if (pSpawn->Anon == 2) {
				strcat_s(szMsg, " \ag*RP*\ax");
			}
			else {
				strcat_s(szMsg, " \ag*Anon*\ax");
			}
		}
		if (gFilterSWho.LD && pSpawn->Linkdead) strcat_s(szMsg, " \ag<LD>\ax");
		if (gFilterSWho.Sneak && pSpawn->Sneak) strcat_s(szMsg, " \ag<Sneak>\ax");
		if (gFilterSWho.AFK && pSpawn->AFK) strcat_s(szMsg, " \ag<AFK>\ax");
		if (gFilterSWho.LFG && pSpawn->LFG) strcat_s(szMsg, " \ag<LFG>\ax");
		if (gFilterSWho.Trader && pSpawn->Trader) strcat_s(szMsg, " \ag<Trader>\ax");
	}
	else if (gFilterSWho.NPCTag && pSpawn->Type == SPAWN_NPC) {
		if (pSpawn->MasterID != 0) {
			strcat_s(szMsg, " <PET>");
		}
		else {
			strcat_s(szMsg, " <NPC>");
		}
	}
	if (gFilterSWho.Light) {
		const char* szLight = GetLightForSpawn(pSpawn);
		if (_stricmp(szLight, "NONE")) {
			strcat_s(szMsg, " (");
			strcat_s(szMsg, szLight);
			strcat_s(szMsg, ")");
		}
	}
	strcpy_s(szMsgL, szMsg);
	if (gFilterSWho.Distance)
	{
		INT Angle = (INT)((atan2f(GetCharInfo()->pSpawn->X - pSpawn->X, GetCharInfo()->pSpawn->Y - pSpawn->Y) * 180.0f / PI + 360.0f) / 22.5f + 0.5f) % 16;
		sprintf_s(szTemp, " \a-u(\ax%1.2f %s\a-u,\ax %1.2fZ\a-u)\ax", GetDistance(GetCharInfo()->pSpawn, pSpawn), szHeadingShort[Angle], pSpawn->Z - GetCharInfo()->pSpawn->Z);
		strcat_s(szMsg, szTemp);
	}
	if (gFilterSWho.SpawnID)
	{
		strcat_s(szMsg, " \a-u(\axID:");
		_itoa_s(pSpawn->SpawnID, szTemp, 10);
		strcat_s(szMsg, szTemp);
		strcat_s(szMsg, "\a-u)\ax");
	}
	if (gFilterSWho.Holding && (pSpawn->Equipment.Primary.ID || pSpawn->Equipment.Offhand.ID))
	{
		strcat_s(szMsg, " \a-u(\ax");
		if (pSpawn->Equipment.Primary.ID)
		{
			_itoa_s(pSpawn->Equipment.Primary.ID, szTemp, 10);
			strcat_s(szMsg, "Pri: ");
			strcat_s(szMsg, szTemp);
			if (pSpawn->Equipment.Offhand.ID)
				strcat_s(szMsg, " ");
		}
		if (pSpawn->Equipment.Offhand.ID)
		{
			_itoa_s(pSpawn->Equipment.Offhand.ID, szTemp, 10);
			strcat_s(szMsg, "Off: ");
			strcat_s(szMsg, szTemp);
		}
		strcat_s(szMsg, "\a-u)\ax");
	}

	switch (GetSpawnType(pSpawn))
	{
	case CHEST:
		strcat_s(szMsg, " \ar*CHEST*\ax");
		break;
	case TRAP:
		strcat_s(szMsg, " \ar*TRAP*\ax");
		break;
	case TRIGGER:
		strcat_s(szMsg, " \ar*TRIGGER*\ax");
		break;
	case TIMER:
		strcat_s(szMsg, " \ar*TIMER*\ax");
		break;
	case UNTARGETABLE:
		strcat_s(szMsg, " \ar*UNTARGETABLE*\ax");
		break;
	}

	WriteChatColor(szMsg, USERCOLOR_WHO);
}

struct SuperWhoSortPredicate
{
	SuperWhoSortPredicate(SearchSortBy sortBy, SPAWNINFO* pSeachOrigin)
		: m_sortBy(sortBy)
		, m_pOrigin(pSeachOrigin)
	{
	}

	bool operator()(SPAWNINFO* SpawnA, SPAWNINFO* SpawnB)
	{
		switch (m_sortBy)
		{
		case SearchSortBy::Level:
			return SpawnA->Level < SpawnB->Level;

		case SearchSortBy::Name:
			return _stricmp(SpawnA->DisplayedName, SpawnB->DisplayedName) < 0;

		case SearchSortBy::Race:
			return _stricmp(pEverQuest->GetRaceDesc(SpawnA->mActorClient.Race), pEverQuest->GetRaceDesc(SpawnB->mActorClient.Race)) < 0;

		case SearchSortBy::Class:
			return _stricmp(GetClassDesc(SpawnA->mActorClient.Class), GetClassDesc(SpawnB->mActorClient.Class)) < 0;

		case SearchSortBy::Distance:
			return GetDistanceSquared(m_pOrigin, SpawnA) < GetDistanceSquared(m_pOrigin, SpawnB);

		case SearchSortBy::Guild:
		{
			char szGuild1[256] = { "" };
			char szGuild2[256] = { "" };
			char* pDest1 = GetGuildByID(SpawnA->GuildID);
			char* pDest2 = GetGuildByID(SpawnB->GuildID);

			if (pDest1)
			{
				strcpy_s(szGuild1, pDest1);
			}

			if (pDest2)
			{
				strcpy_s(szGuild2, pDest2);
			}

			return _stricmp(szGuild1, szGuild2) < 0;
		}

		case SearchSortBy::Id:
		default:
			return SpawnA->SpawnID < SpawnB->SpawnID;
		}
	}

private:
	SearchSortBy m_sortBy;
	SPAWNINFO* m_pOrigin;
};

void SuperWhoDisplay(SPAWNINFO* pChar, MQSpawnSearch* pSearchSpawn, DWORD Color)
{
	if (!pSearchSpawn)
		return;

	std::vector<SPAWNINFO*> SpawnSet;

	SPAWNINFO* pSpawn = (SPAWNINFO*)pSpawnList;
	SPAWNINFO* pOrigin = nullptr;

	if (pSearchSpawn->FromSpawnID)
		pOrigin = (SPAWNINFO*)GetSpawnByID(pSearchSpawn->FromSpawnID);
	if (!pOrigin)
		pOrigin = pChar;

	while (pSpawn)
	{
		if (SpawnMatchesSearch(pSearchSpawn, pOrigin, pSpawn))
		{
			// matches search, add to our set
			SpawnSet.push_back(pSpawn);
		}

		pSpawn = pSpawn->pNext;
	}

	if (!SpawnSet.empty())
	{
		if (SpawnSet.size() > 1)
		{
			// sort our list
			std::sort(std::begin(SpawnSet), std::end(SpawnSet),
				SuperWhoSortPredicate{ pSearchSpawn->SortBy, pOrigin });
		}

		WriteChatColor("List of matching spawns", USERCOLOR_WHO);
		WriteChatColor("--------------------------------", USERCOLOR_WHO);

		for (SPAWNINFO* spawn : SpawnSet)
		{
			SuperWhoDisplay(spawn, Color);
		}

		char* pszSpawnType = nullptr;
		switch (pSearchSpawn->SpawnType)
		{
		case NONE:
		default:
			pszSpawnType = "any";
			break;
		case PC:
			pszSpawnType = "pc";
			break;
		case MOUNT:
			pszSpawnType = "mount";
			break;
		case PET:
			pszSpawnType = "pet";
			break;
		case PCPET:
			pszSpawnType = "pcpet";
			break;
		case NPCPET:
			pszSpawnType = "npcpet";
			break;
		case XTARHATER:
			pszSpawnType = "xtarhater";
			break;
		case NPC:
			pszSpawnType = "npc";
			break;
		case CORPSE:
			pszSpawnType = "corpse";
			break;
		case TRIGGER:
			pszSpawnType = "trigger";
			break;
		case TRAP:
			pszSpawnType = "trap";
			break;
		case CHEST:
			pszSpawnType = "chest";
			break;
		case TIMER:
			pszSpawnType = "timer";
			break;
		case UNTARGETABLE:
			pszSpawnType = "untargetable";
			break;
		case MERCENARY:
			pszSpawnType = "mercenary";
			break;
		case FLYER:
			pszSpawnType = "flyer";
			break;
		}

		if (CHARINFO * pCharinf = GetCharInfo())
		{
			size_t count = SpawnSet.size();

			WriteChatf("There %s \ag%d\ax %s%s in %s.",
				(count == 1) ? "is" : "are", count, pszSpawnType, (count == 1) ? "" : "s", GetFullZone(pCharinf->zoneId));
		}
	}
	else
	{
		WriteChatColor("List of matching spawns", USERCOLOR_WHO);
		WriteChatColor("--------------------------------", USERCOLOR_WHO);

		char szMsg[MAX_STRING] = { 0 };
		FormatSearchSpawn(szMsg, sizeof(szMsg), pSearchSpawn);
		strcat_s(szMsg, " was not found.");
		WriteChatColor(szMsg, USERCOLOR_WHO);
	}
}

float StateHeightMultiplier(DWORD StandState)
{
	switch (StandState) {
	case STANDSTATE_BIND:
	case STANDSTATE_DUCK:
		return 0.5f;
	case STANDSTATE_SIT:
		return 0.3f;
	case STANDSTATE_FEIGN:
	case STANDSTATE_DEAD:
		return 0.1f;
	case STANDSTATE_STAND:
	default:
		return 0.9f;
	}
}

int FindSpellListByName(const char* szName)
{
	for (int Index = 0; Index < NUM_SPELL_SETS; Index++)
	{
		if (!_stricmp(pSpellSets[Index].Name, szName))
			return Index;
	}

	return -1;
}

char* GetFriendlyNameForGroundItem(PGROUNDITEM pItem, char* szName, size_t BufferSize)
{
	szName[0] = 0;
	if (!pItem)
		return &szName[0];
	DWORD Item = atoi(pItem->Name + 2);
	ACTORDEFENTRY* ptr = ActorDefList;
	while (ptr->Def)
	{
		if (ptr->Def == Item
			&& (ptr->ZoneID && (ptr->ZoneID < 0 || ptr->ZoneID == (pItem->ZoneID & 0x7FFF))))
		{
			sprintf_s(szName, BufferSize, "%s", ptr->Name);
			return &szName[0];
		}
		ptr++;

	}

	sprintf_s(szName, BufferSize, "Drop%05d/%d", Item, pItem->DropID);
	return szName;
}

void WriteFilterNames()
{
	char szBuffer[MAX_STRING] = { 0 };
	int filternumber = 1;

	MQFilter* pFilter = gpFilters;
	WritePrivateProfileSection("Filter Names", szBuffer, gszINIFilename);
	while (pFilter) {
		if (pFilter->pEnabled == &gFilterCustom) {
			sprintf_s(szBuffer, "Filter%d", filternumber++);
			WritePrivateProfileString("Filter Names", szBuffer, pFilter->FilterText, gszINIFilename);
		}
		pFilter = pFilter->pNext;
	}

}

bool GetShortBuffID(SPELLBUFF* pBuff, int& nID)
{
	CHARINFO2* pChar = GetCharInfo2();
	unsigned long N = (pBuff - &pChar->ShortBuff[0]);
	if (N < NUM_SHORT_BUFFS)
	{
		nID = N + 1;
		return true;
	}
	return false;
}

bool GetBuffID(SPELLBUFF* pBuff, int& nID)
{
	CHARINFO2* pChar = GetCharInfo2();
	int index = (pBuff - &pChar->Buff[0]);

	if (index < NUM_LONG_BUFFS)
	{
		nID = index + 1;
		return true;
	}
	return false;
}

#define IS_SET(flag, bit)   ((flag) & (bit))
#define LDON_Non    0
#define LDON_DG     1
#define LDON_MIR    2
#define LDON_MIS    4
#define LDON_RUJ    8
#define LDON_TAK    16

char* GetLDoNTheme(DWORD LDTheme)
{
	if (LDTheme == 31) return "All";
	if (IS_SET(LDTheme, LDON_DG)) return "Deepest Guk";
	if (IS_SET(LDTheme, LDON_MIR)) return "Miragul's";
	if (IS_SET(LDTheme, LDON_MIS)) return "Mistmoore";
	if (IS_SET(LDTheme, LDON_RUJ)) return "Rujarkian";
	if (IS_SET(LDTheme, LDON_TAK)) return "Takish";
	return "Unknown";
}

uint32_t GetItemTimer(CONTENTS* pItem)
{
	uint32_t Timer = pPCData->GetItemRecastTimer((EQ_Item*)& pItem, eActivatableSpell);

	if (Timer < GetFastTime())
		return 0;

	return Timer - GetFastTime();
}

CONTENTS* GetItemContentsBySlotID(DWORD dwSlotID)
{
	int InvSlot = -1;
	int SubSlot = -1;
	if (dwSlotID >= 0 && dwSlotID < NUM_INV_SLOTS) InvSlot = dwSlotID;
	else if (dwSlotID >= 262 && dwSlotID < 342) {
		InvSlot = BAG_SLOT_START + (dwSlotID - 262) / 10;
		SubSlot = (dwSlotID - 262) % 10;
	}
	if (InvSlot >= 0 && InvSlot < NUM_INV_SLOTS) {
		if (CHARINFO2 * pChar2 = GetCharInfo2()) {
			if (pChar2->pInventoryArray) {
				if (CONTENTS * iSlot = pChar2->pInventoryArray->InventoryArray[InvSlot]) {
					if (SubSlot < 0)
						return iSlot;
					if (pChar2->pInventoryArray->InventoryArray[InvSlot]->Contents.ContainedItems.pItems) {
						if (CONTENTS * sSlot = pChar2->pInventoryArray->InventoryArray[InvSlot]->GetContent(SubSlot)) {
							return sSlot;
						}
					}
				}
			}
		}
	}
	return nullptr;
}

CONTENTS* GetItemContentsByName(const char* ItemName)
{
	if (CHARINFO2 * pChar2 = GetCharInfo2()) {
		if (pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray) {
			for (unsigned long nSlot = 0; nSlot < NUM_INV_SLOTS; nSlot++) {
				if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[nSlot]) {
					if (!_stricmp(ItemName, GetItemFromContents(pItem)->Name)) {
						return pItem;
					}
				}
			}
			for (unsigned long nPack = 0; nPack < 10; nPack++) {
				if (CONTENTS * pPack = pChar2->pInventoryArray->Inventory.Pack[nPack]) {
					if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
						for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
							if (CONTENTS * pItem = pPack->GetContent(nItem)) {
								if (!_stricmp(ItemName, GetItemFromContents(pItem)->Name)) {
									return pItem;
								}
							}
						}
					}
				}
			}
		}
	}
	return nullptr;
}

CXWnd* GetParentWnd(CXWnd* pWnd)
{
	while (pWnd)
	{
		if (!pWnd->GetParentWindow())
			return pWnd;

		pWnd = pWnd->GetParentWindow();
	}

	return nullptr;
}

bool LoH_HT_Ready()
{
	DWORD i = ((SPAWNINFO*)pLocalPlayer)->SpellGemETA[InnateETA];
	DWORD j = i - ((CDISPLAY*)pDisplay)->TimeStamp;
	if (i < j) return true;
	return false;
}

int GetSkillIDFromName(const char* name)
{
	for (int i = 0; i < NUM_SKILLS; i++)
	{
		if (SKILL * pSkill = pSkillMgr->pSkill[i])
		{
			if (!_stricmp(name, pStringTable->getString(pSkill->nName)))
				return i;
		}
	}

	return 0;
}

bool InHoverState()
{
	if (GetCharInfo() && GetCharInfo()->Stunned == 3)
		return true;
	return false;
}

DWORD GetGameState()
{
	if (!ppEverQuest || !pEverQuest)
	{
		//DebugSpew("Could not retrieve gamestate in GetGameState()");
		return -1;
	}
	return ((EVERQUEST*)pEverQuest)->GameState;
}

DWORD GetWorldState()
{
	if (!ppEverQuest || !pEverQuest)
	{
		//DebugSpew("Could not retrieve worldstate in GetWorldState()");
		return -1;
	}
	return ((EVERQUEST*)pEverQuest)->WorldState;
}
// ***************************************************************************
// Function:    LargerEffectTest
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL LargerEffectTest(PSPELL aSpell, PSPELL bSpell, int i, BOOL bTriggeredEffectCheck)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	LONG bAttrib = GetSpellNumEffects(bSpell) > i ? GetSpellAttrib(bSpell, i) : 254;
	if (aAttrib == bAttrib)			// verify they are the same, we can do fewer checks this way
//		&& (aAttrib == 1			// Ac Mod
//			|| aAttrib == 2			// ATK*
//			|| aAttrib == 15		// Mana*
//			|| aAttrib == 55		// Add Effect: Absorb Damage
//			|| aAttrib == 69		// Max HP Mod
//			|| aAttrib == 79		// HP Mod
//			|| aAttrib == 114		// Aggro Multiplier
//			|| aAttrib == 127		// Spell Haste
//			|| aAttrib == 162))		// Mitigate Melee Damage*
									// We don't need to check NumEffects again since it wouldn't reach here if it would be too big
		return (abs(GetSpellBase(aSpell, i)) >= abs(GetSpellBase(bSpell, i)) || (bTriggeredEffectCheck && (aSpell->SpellGroup == bSpell->SpellGroup)));
	return false;
}

// ***************************************************************************
// Function:    TriggeringEffectSpell
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL TriggeringEffectSpell(PSPELL aSpell, int i)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	return (aAttrib == 85	// Add Proc
		|| aAttrib == 374 	// Trigger Spell
		|| aAttrib == 419);	// Contact_Ability_2
}

// ***************************************************************************
// Function:    SpellEffectTest
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL SpellEffectTest(PSPELL aSpell, PSPELL bSpell, int i, BOOL bIgnoreTriggeringEffects, BOOL bTriggeredEffectCheck = FALSE)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	LONG bAttrib = GetSpellNumEffects(bSpell) > i ? GetSpellAttrib(bSpell, i) : 254;
	return ((aAttrib == 57 || bAttrib == 57)		// Levitate
		|| (aAttrib == 134 || bAttrib == 134)		// Limit: Max Level
		|| (aAttrib == 135 || bAttrib == 135)		// Limit: Resist
		|| (aAttrib == 136 || bAttrib == 136)		// Limit: Target
		|| (aAttrib == 137 || bAttrib == 137)		// Limit: Effect
		|| (aAttrib == 138 || bAttrib == 138)		// Limit: SpellType
		|| (aAttrib == 139 || bAttrib == 139)		// Limit: Spell
		|| (aAttrib == 140 || bAttrib == 140)		// Limit: Min Duraction
		|| (aAttrib == 141 || bAttrib == 141)		// Limit: Instant
		|| (aAttrib == 142 || bAttrib == 142)		// Limit: Min Level
		|| (aAttrib == 143 || bAttrib == 143)		// Limit: Min Cast Time
		|| (aAttrib == 144 || bAttrib == 144)		// Limit: Max Cast Time
		|| (aAttrib == 254 || bAttrib == 254)		// Placeholder
		|| (aAttrib == 311 || bAttrib == 311)		// Limit: Combat Skills not Allowed
		|| (aAttrib == 339 || bAttrib == 339)		// Trigger DoT on cast
		|| (aAttrib == 340 || bAttrib == 340)		// Trigger DD on cast
		|| (aAttrib == 348 || bAttrib == 348)		// Limit: Min Mana
//		|| (aAttrib == 374 || bAttrib == 374)		// Add Effect: xxx		
|| (aAttrib == 385 || bAttrib == 385)		// Limit: SpellGroup
|| (aAttrib == 391 || bAttrib == 391)		// Limit: Max Mana
|| (aAttrib == 403 || bAttrib == 403)		// Limit: SpellClass
|| (aAttrib == 404 || bAttrib == 404)		// Limit: SpellSubclass
|| (aAttrib == 411 || bAttrib == 411)		// Limit: PlayerClass
|| (aAttrib == 412 || bAttrib == 412)		// Limit: Race
|| (aAttrib == 414 || bAttrib == 414)		// Limit: CastingSkill
|| (aAttrib == 422 || bAttrib == 422)		// Limit: Use Min
|| (aAttrib == 423 || bAttrib == 423)		// Limit: Use Type
|| (aAttrib == 428 || bAttrib == 428)		// Skill_Proc_Modifier
|| (LargerEffectTest(aSpell, bSpell, i, bTriggeredEffectCheck))	// Ignore if the new effect is greater than the old effect
|| (bIgnoreTriggeringEffects && (TriggeringEffectSpell(aSpell, i) || TriggeringEffectSpell(bSpell, i)))		// Ignore triggering effects validation
|| ((aSpell->SpellType == 1 || aSpell->SpellType == 2) && (bSpell->SpellType == 1 || bSpell->SpellType == 2) && !(aSpell->DurationWindow == bSpell->DurationWindow)));
}

// ***************************************************************************
// Function:    BuffStackTest
// Description: Return boolean true if the two spells will stack
// Usage:       Used by ${Spell[xxx].Stacks}, ${Spell[xxx].StacksPet},
//                ${Spell[xxx].WillStack[yyy]}, ${Spell[xxx].StacksWith[yyy]}
// Author:      Pinkfloydx33
// ***************************************************************************
BOOL BuffStackTest(PSPELL aSpell, PSPELL bSpell, BOOL bIgnoreTriggeringEffects, BOOL bTriggeredEffectCheck)
{
	PSPAWNINFO pSpawn = (PSPAWNINFO)pLocalPlayer;
	if (!pSpawn || !pSpawn->GetCharacter())
		return true;
	if (GetGameState() != GAMESTATE_INGAME)
		return true;
	if (gZoning)
		return true;
	//CharacterZoneClient*pCZC = (CharacterZoneClient*)pSpawn->spawneqc_info;
	//PCHARINFO pCharInfo = GetCharInfo();
	//CharacterBase*cb = (CharacterBase*)&pCharInfo->CharacterBase_vftable;
	//const BaseProfile*pBP = &pCZC->GetCurrentBaseProfile();
	//int Class = pCZC->GetCurrentBaseProfile().Class;
	//if (IsBadReadPtr((void*)pCZC, 4))
	//	return true;
	//if (IsBadReadPtr((void*)&pCZC->GetCurrentBaseProfile().Class, 4)) 
	//	return true;
	if (!aSpell || !bSpell)
		return false;
	if (IsBadReadPtr((void*)aSpell, 4))
		return false;
	if (IsBadReadPtr((void*)bSpell, 4))
		return false;
	if (aSpell->ID == bSpell->ID)
		return true;

	if (gStackingDebug) {
		char szStackingDebug[MAX_STRING] = { 0 };
		snprintf(szStackingDebug, sizeof(szStackingDebug), "aSpell->Name=%s(%d) bSpell->Name=%s(%d)", aSpell->Name, aSpell->ID, bSpell->Name, bSpell->ID);
		DebugSpewAlwaysFile("%s", szStackingDebug);
		if (gStackingDebug == -1)
			WriteChatColor(szStackingDebug, USERCOLOR_CHAT_CHANNEL);
	}

	//EQ_Affect eff = { 0 };
	//eff.ID = bSpell->ID;
	//bool bItWillNotStack = ((CharacterZoneClient*)((PSPAWNINFO)pLocalPlayer)->spawneqc_info)->IsStackBlocked((EQ_Spell*)aSpell, (PSPAWNINFO)pLocalPlayer, &eff, 1);
	//if (bItWillNotStack) {
		//Sleep(0);
		//WriteChatf("EQ Client says spell is BLOCKED");
	//	return false;
	//}
	//else {
	//	return true;
	//}

	// We need to loop over the largest of the two, this may seem silly but one could have stacking command blocks
	// which we will always need to check.
	LONG effects = std::max(GetSpellNumEffects(aSpell), GetSpellNumEffects(bSpell));
	for (int i = 0; i < effects; i++) {
		//Compare 1st Buff to 2nd. If Attrib[i]==254 its a place holder. If it is 10 it
		//can be 1 of 3 things: PH(Base=0), CHA(Base>0), Lure(Base=-6). If it is Lure or
		//Placeholder, exclude it so slots don't match up. Now Check to see if the slots
		//have equal attribute values. If the do, they don't stack.
		LONG aAttrib = 254, bAttrib = 254; // Default to placeholder ...
		LONG aBase = 0, bBase = 0, aBase2 = 0, bBase2 = 0;
		if (GetSpellNumEffects(aSpell) > i) {
			aAttrib = GetSpellAttrib(aSpell, i);
			aBase = GetSpellBase(aSpell, i);
			aBase2 = GetSpellBase2(aSpell, i);
		}
		if (GetSpellNumEffects(bSpell) > i) {
			bAttrib = GetSpellAttrib(bSpell, i);
			bBase = GetSpellBase(bSpell, i);
			bBase2 = GetSpellBase2(bSpell, i);
		}
		if (gStackingDebug) {
			char szStackingDebug[MAX_STRING] = { 0 };
			snprintf(szStackingDebug, sizeof(szStackingDebug), "Slot %d: bSpell->Attrib=%d, bSpell->Base=%d, bSpell->TargetType=%d, aSpell->Attrib=%d, aSpell->Base=%d, aSpell->TargetType=%d", i, bAttrib, bBase, bSpell->TargetType, aAttrib, aBase, aSpell->TargetType);
			DebugSpewAlwaysFile("%s", szStackingDebug);
			if (gStackingDebug == -1)
				WriteChatColor(szStackingDebug, USERCOLOR_CHAT_CHANNEL);
		}
		BOOL bTriggerA = TriggeringEffectSpell(aSpell, i);
		BOOL bTriggerB = TriggeringEffectSpell(bSpell, i);
		if (bTriggerA || bTriggerB) {
			PSPELL pRetSpellA = GetSpellByID(bTriggerA ? (aAttrib == 374 ? aBase2 : aBase) : aSpell->ID);
			PSPELL pRetSpellB = GetSpellByID(bTriggerB ? (bAttrib == 374 ? bBase2 : bBase) : bSpell->ID);
			if (!pRetSpellA || !pRetSpellB)
				if (gStackingDebug) {
					char szStackingDebug[MAX_STRING] = { 0 };
					snprintf(szStackingDebug, sizeof(szStackingDebug), "BuffStackTest ERROR: aSpell[%d]:%s%s, bSpell[%d]:%s%s", aSpell->ID, aSpell->Name, pRetSpellA ? "" : "is null", bSpell->ID, bSpell->Name, pRetSpellB ? "" : "is null");
					DebugSpewAlwaysFile("%s", szStackingDebug);
					if (gStackingDebug == -1)
						WriteChatColor(szStackingDebug, USERCOLOR_CHAT_CHANNEL);
				}
			if (!((bTriggerA && (aSpell->ID == pRetSpellA->ID)) || (bTriggerB && (bSpell->ID == pRetSpellB->ID)))) {
				if (!BuffStackTest(pRetSpellA, pRetSpellB, bIgnoreTriggeringEffects, true)) {
					if (gStackingDebug) {
						DebugSpewAlwaysFile("returning FALSE #1");
						if (gStackingDebug == -1)
							WriteChatColor("returning FALSE #1", USERCOLOR_CHAT_CHANNEL);
					}
					return false;
				}
			}
		}
		if (bAttrib == aAttrib && !SpellEffectTest(aSpell, bSpell, i, bIgnoreTriggeringEffects, bTriggeredEffectCheck)) {
			if (gStackingDebug) {
				DebugSpewAlwaysFile("Inside IF");
				if (gStackingDebug == -1)
					WriteChatColor("Inside IF", USERCOLOR_CHAT_CHANNEL);
			}
			//			if (aAttrib == 55 && bAttrib == 55) {	//Mitigate Melee Damage
			//				return (aBase >= bBase);
			//			}
			//			else 
			if (!((bAttrib == 10 && (bBase == -6 || bBase == 0)) ||
				(aAttrib == 10 && (aBase == -6 || aBase == 0)) ||
				(bAttrib == 79 && bBase > 0 && bSpell->TargetType == 6) ||
				(aAttrib == 79 && aBase > 0 && aSpell->TargetType == 6) ||
				(bAttrib == 0 && bBase < 0) ||
				(aAttrib == 0 && aBase < 0) ||
				(bAttrib == 148 || bAttrib == 149) ||
				(aAttrib == 148 || aAttrib == 149))) {
				if (gStackingDebug) {
					DebugSpewAlwaysFile("returning FALSE #2");
					if (gStackingDebug == -1)
						WriteChatColor("returning FALSE #2", USERCOLOR_CHAT_CHANNEL);
				}
				return false;
			}
		}
		//Check to see if second buffs blocks first buff:
		//148: Stacking: Block new spell if slot %d is effect
		//149: Stacking: Overwrite existing spell if slot %d is effect
		if (bAttrib == 148 || bAttrib == 149) {
			// in this branch we know bSpell has enough slots
			int tmpSlot = (bAttrib == 148 ? bBase2 - 1 : GetSpellCalc(bSpell, i) - 200 - 1);
			int tmpAttrib = bBase;
			if (GetSpellNumEffects(aSpell) > tmpSlot) { // verify aSpell has that slot
				if (gStackingDebug) {
					char szStackingDebug[MAX_STRING] = { 0 };
					snprintf(szStackingDebug, sizeof(szStackingDebug), "aSpell->Attrib[%d]=%d, aSpell->Base[%d]=%d, tmpAttrib=%d, tmpVal=%d", tmpSlot, GetSpellAttrib(aSpell, tmpSlot), tmpSlot, GetSpellBase(aSpell, tmpSlot), tmpAttrib, abs(GetSpellMax(bSpell, i)));
					DebugSpewAlwaysFile("%s", szStackingDebug);
					if (gStackingDebug == -1)
						WriteChatColor(szStackingDebug, USERCOLOR_CHAT_CHANNEL);
				}
				if (GetSpellMax(bSpell, i) > 0) {
					int tmpVal = abs(GetSpellMax(bSpell, i));
					if (GetSpellAttrib(aSpell, tmpSlot) == tmpAttrib && GetSpellBase(aSpell, tmpSlot) < tmpVal) {
						if (gStackingDebug) {
							DebugSpewAlwaysFile("returning FALSE #3");
							if (gStackingDebug == -1)
								WriteChatColor("returning FALSE #3", USERCOLOR_CHAT_CHANNEL);
						}
						return false;
					}
				}
				else if (GetSpellAttrib(aSpell, tmpSlot) == tmpAttrib) {
					if (gStackingDebug) {
						DebugSpewAlwaysFile("returning FALSE #4");
						if (gStackingDebug == -1)
							WriteChatColor("returning FALSE #4", USERCOLOR_CHAT_CHANNEL);
					}
					return false;
				}
			}
		}
		/*
		//Now Check to see if the first buff blocks second buff. This is necessary
		//because only some spells carry the Block Slot. Ex. Brells and Spiritual
		//Vigor don't stack Brells has 1 slot total, for HP. Vigor has 4 slots, 2
		//of which block Brells.
		if (aAttrib == 148 || aAttrib == 149) {
			// in this branch we know aSpell has enough slots
			int tmpSlot = (aAttrib == 148 ? aBase2 - 1 : GetSpellCalc(aSpell, i) - 200 - 1);
			int tmpAttrib = aBase;
			if (GetSpellNumEffects(bSpell) > tmpSlot) { // verify bSpell has that slot
				if (gStackingDebug) {
					char szStackingDebug[MAX_STRING] = { 0 };
					snprintf(szStackingDebug, sizeof(szStackingDebug), "bSpell->Attrib[%d]=%d, bSpell->Base[%d]=%d, tmpAttrib=%d, tmpVal=%d", tmpSlot, GetSpellAttrib(bSpell, tmpSlot), tmpSlot, GetSpellBase(bSpell, tmpSlot), tmpAttrib, abs(GetSpellMax(aSpell, i)));
					DebugSpewAlwaysFile("%s", szStackingDebug);
					if (gStackingDebug == -1)
						WriteChatColor(szStackingDebug, USERCOLOR_CHAT_CHANNEL);
				}
				if (GetSpellMax(aSpell, i) > 0) {
					int tmpVal = abs(GetSpellMax(aSpell, i));
					if (GetSpellAttrib(bSpell, tmpSlot) == tmpAttrib && GetSpellBase(bSpell, tmpSlot) < tmpVal) {
						if (gStackingDebug) {
							DebugSpewAlwaysFile("returning FALSE #5");
							if (gStackingDebug == -1)
								WriteChatColor("returning FALSE #5", USERCOLOR_CHAT_CHANNEL);
						}
						return false;
					}
				}
				else if (GetSpellAttrib(bSpell, tmpSlot) == tmpAttrib) {
					if (gStackingDebug) {
						DebugSpewAlwaysFile("returning FALSE #6");
						if (gStackingDebug == -1)
							WriteChatColor("returning FALSE #6", USERCOLOR_CHAT_CHANNEL);
					}
					return false;
				}
			}
		}
		*/
	}
	if (gStackingDebug) {
		DebugSpewAlwaysFile("returning TRUE");
		if (gStackingDebug == -1)
			WriteChatColor("returning TRUE", USERCOLOR_CHAT_CHANNEL);
	}
	return true;
}

#if 0
BOOL BuffStackTestOld(PSPELL aSpell, PSPELL bSpell, BOOL bIgnoreTriggeringEffects)
{
	if (aSpell->ID == bSpell->ID) return true;

	//WriteChatf("aSpell->Name=%s bSpell->Name= %s", aSpell->Name, bSpell->Name);
	int i;
	for (i = 0; i < GetSpellNumEffects(aSpell); i++) {
		//Compare 1st Buff to 2nd. If Attrib[i]==254 its a place holder. If it is 10 it
		//can be 1 of 3 things: PH(Base=0), CHA(Base>0), Lure(Base=-6). If it is Lure or
		//Placeholder, exclude it so slots don't match up. Now Check to see if the slots
		//have equal attribute values. If the do, they don't stack.
		//WriteChatf("Slot %d: bSpell->Attrib=%d, bSpell->Base=%d, bSpell->TargetType=%d, aSpell->Attrib=%d, aSpell->Base=%d, aSpell->TargetType=%d", i, GetSpellAttrib(bSpell,i), GetSpellBase(bSpell,i), bSpell->TargetType, GetSpellAttrib(aSpell,i), GetSpellBase(aSpell,i), aSpell->TargetType);
		if (GetSpellAttrib(bSpell, i) == GetSpellAttrib(aSpell, i) && !(SpellEffectTest(aSpell, bSpell, i, bIgnoreTriggeringEffects))) {
			//WriteChatf("Inside IF");
			if (GetSpellAttrib(aSpell, i) == 55 && GetSpellAttrib(bSpell, i) == 55) {
				//WriteChatf("Increase Absorb Damage by %d over %d", GetSpellBase(aSpell,i), GetSpellBase(bSpell,i));
				return (GetSpellBase(aSpell, i) >= GetSpellBase(bSpell, i));
			}
			else if (!((GetSpellAttrib(bSpell, i) == 10 && (GetSpellBase(bSpell, i) == -6 || GetSpellBase(bSpell, i) == 0)) ||
				(GetSpellAttrib(aSpell, i) == 10 && (GetSpellBase(aSpell, i) == -6 || GetSpellBase(aSpell, i) == 0)) ||
				(GetSpellAttrib(bSpell, i) == 79 && GetSpellBase(bSpell, i) > 0 && bSpell->TargetType == 6) ||
				(GetSpellAttrib(aSpell, i) == 79 && GetSpellBase(aSpell, i) > 0 && aSpell->TargetType == 6) ||
				(GetSpellAttrib(bSpell, i) == 0 && GetSpellBase(bSpell, i) < 0) ||
				(GetSpellAttrib(aSpell, i) == 0 && GetSpellBase(aSpell, i) < 0) ||
				(GetSpellAttrib(bSpell, i) == 148 || GetSpellAttrib(bSpell, i) == 149) ||
				(GetSpellAttrib(aSpell, i) == 148 || GetSpellAttrib(aSpell, i) == 149)))
				return false;
		}
		//Check to see if second buffs blocks first buff:
		//148: Stacking: Block new spell if slot %d is effect
		//149: Stacking: Overwrite existing spell if slot %d is effect
		if ((GetSpellAttrib(bSpell, i) == 148) || (GetSpellAttrib(bSpell, i) == 149)) {
			int tmpSlot = GetSpellCalc(bSpell, i) - 200;
			int tmpAttrib = GetSpellBase(bSpell, i);
			//WriteChatf("aSpell->Attrib[%d]=%d, aSpell->Base[%d]=%d, tmpAttrib=%d, tmpVal=%d", tmpSlot-1, aSpell->Attrib[tmpSlot-1], tmpSlot-1, aSpell->Base[tmpSlot-1], tmpAttrib, abs(GetSpellMax(bSpell,i)));
			if (GetSpellMax(bSpell, i) > 0) {
				int tmpVal = abs(GetSpellMax(bSpell, i));
				if ((GetSpellAttrib(aSpell, tmpSlot - 1) == tmpAttrib) && (GetSpellBase(aSpell, tmpSlot - 1) < tmpVal)) return false;
			}
			else if (GetSpellAttrib(aSpell, tmpSlot - 1) == tmpAttrib) return false;
		}
		//Now Check to see if the first buff blocks second buff. This is necessary 
		//because only some spells carry the Block Slot. Ex. Brells and Spiritual 
		//Vigor don't stack Brells has 1 slot total, for HP. Vigor has 4 slots, 2 
		//of which block Brells.
		if ((GetSpellAttrib(aSpell, i) == 148) || (GetSpellAttrib(aSpell, i) == 149)) {
			int tmpSlot = GetSpellCalc(aSpell, i) - 200;
			int tmpAttrib = GetSpellBase(aSpell, i);
			//WriteChatf("bSpell->Attrib[%d]=%d, bSpell->Base[%d]=%d, tmpAttrib=%d, tmpVal=%d", tmpSlot-1, bSpell->Attrib[tmpSlot-1], tmpSlot-1, bSpell->Base[tmpSlot-1], tmpAttrib, abs(GetSpellMax(aSpell,i)));
			if (GetSpellMax(aSpell, i) > 0) {
				int tmpVal = abs(GetSpellMax(aSpell, i));
				if ((GetSpellAttrib(bSpell, tmpSlot - 1) == tmpAttrib) && (GetSpellBase(bSpell, tmpSlot - 1) < tmpVal)) return false;
			}
			else if (GetSpellAttrib(bSpell, tmpSlot - 1) == tmpAttrib) return false;
		}
	}
	return true;
}
#endif

float GetMeleeRange(PlayerClient* pSpawn1, PlayerClient* pSpawn2)
{
	float f, g, h;
	if (pSpawn1 && pSpawn2)
	{
		f = ((SPAWNINFO*)pSpawn1)->GetMeleeRangeVar1 * ((SPAWNINFO*)pSpawn1)->MeleeRadius;
		g = ((SPAWNINFO*)pSpawn2)->GetMeleeRangeVar1 * ((SPAWNINFO*)pSpawn2)->MeleeRadius;

		h = abs(((SPAWNINFO*)pSpawn1)->AvatarHeight - ((SPAWNINFO*)pSpawn2)->AvatarHeight);

		f = (f + g) * 0.75f;

		if (f < 14.0f)
			f = 14.0f;

		g = f + 2 + h;

		if (g > 75.0f)
			return 75.0f;
		else
			return g;
	}
	return 14.0f;
}
DWORD GetSpellGemTimer2(int nGem);
//todo: check manually
DWORD GetSpellGemTimer(DWORD nGem)
{
	return GetSpellGemTimer2(nGem);
}
bool IsValidSpellIndex(int index)
{
	if ((index < 1) || (index > TOTAL_SPELL_COUNT))
		return false;
	return true;
}
inline bool IsValidSpellSlot(int nGem)
{
	return nGem >= 0 && nGem < 16;
}
//testing new cooldown code... -eqmule work in progress
DWORD GetSpellGemTimer2(int nGem)
{
	//int TheID = ((PSPAWNINFO)pLocalPlayer)->CastingData.SpellID;
	//bool bValid = IsValidSpellIndex(TheID);
	bool bValidSlot = IsValidSpellSlot(nGem);
	//if (bValid == false && bValidSlot)
	if (bValidSlot)
	{
		int memspell = GetMemorizedSpell(nGem);
		if (PSPELL pSpell = GetSpellByID(memspell))
		{
			int ReuseTimerIndex = pSpell->ReuseTimerIndex;
			UINT linkedtimer = ((PcZoneClient*)pPCData)->GetLinkedSpellReuseTimer(ReuseTimerIndex);
			__time32_t RecastTime = ReuseTimerIndex > 0 && ReuseTimerIndex < 25 ? linkedtimer : 0;
			UINT RecastDuration = 0;
			UINT LinkedDuration = 0;
			UINT gemeta = ((SPAWNINFO*)pLocalPlayer)->SpellGemETA[nGem];
			DWORD now = ((CDISPLAY*)pDisplay)->TimeStamp;
			if (gemeta > now)
			{
				RecastDuration = gemeta - now;
			}
			__time32_t fasttime = (__time32_t)GetFastTime();
			if (RecastTime > fasttime)
			{
				LinkedDuration = (RecastTime - fasttime) * 1000;
			}
			CSpellGemWnd* gem = pCastSpellWnd->SpellSlots[nGem];
			UINT Timer = std::max(RecastDuration, LinkedDuration);
			UINT timeremaining = gem->GetCoolDownTimeRemaining();
			UINT totaldur = gem->GetCoolDownTotalDuration();

			bool TimerChanged = !(abs(long(Timer - timeremaining)) < 1000);
			if (Timer > 0 && (totaldur == 0 || TimerChanged))
			{
				int TotalDuration = Timer;
				if (RecastDuration > LinkedDuration)
				{
					VePointer<CONTENTS> pFocusItem;
					int ReuseMod = pCharData->GetFocusReuseMod((EQ_Spell*)pSpell, pFocusItem);
					TotalDuration = pSpell->RecastTime - ReuseMod;
				}
				//do stuff
				return TotalDuration;
			}
			return Timer;
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
	return 0;
}
DWORD GetSpellBuffTimer(DWORD SpellID)
{
	for (unsigned long nBuff = 0; nBuff < NUM_LONG_BUFFS; nBuff++) {
		if (pBuffWnd->BuffId[nBuff] == SpellID) {
			if (pBuffWnd->BuffTimer[nBuff]) {
				return pBuffWnd->BuffTimer[nBuff];
			}
		}
	}

	// look, this probably is an oversight by the eqdevs
	// but the struct only have 0x2a BuffTimers so...
	// even though there could be 0x37 shortbuffs
	// we can only check up to 0x2a...
	for (unsigned long nBuff = 0; nBuff < NUM_LONG_BUFFS; nBuff++) {
		if (pSongWnd->BuffId[nBuff] == SpellID) {
			if (pSongWnd->BuffTimer[nBuff]) {
				return pSongWnd->BuffTimer[nBuff];
			}
		}
	}
	return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// Functions that were built into commands and people used DoCommand to execute                  //
void AttackRanged(PlayerClient* pRangedTarget)
{
	if (pRangedTarget && gbRangedAttackReady)
	{
		pLocalPlayer->DoAttack(0x0B, 0, pRangedTarget);
		gbRangedAttackReady = 0;
	}
}

void UseAbility(char* sAbility) {

	char szBuffer[MAX_STRING] = { 0 };
	sprintf_s(szBuffer, "%s", sAbility);

	if (!cmdDoAbility)
	{
		PCMDLIST pCmdListOrig = (PCMDLIST)EQADDR_CMDLIST;
		for (int i = 0; pCmdListOrig[i].fAddress != 0; i++) {
			if (!strcmp(pCmdListOrig[i].szName, "/doability")) {
				cmdDoAbility = (fEQCommand)pCmdListOrig[i].fAddress;
			}
		}
	}
	if (!cmdDoAbility) return;

	//if (argc<2 || atoi(argv[1]) || !EQADDR_DOABILITYLIST) {
	if (atoi(szBuffer) || !EQADDR_DOABILITYLIST) {
		cmdDoAbility((PSPAWNINFO)pLocalPlayer, szBuffer);
		return;
	}

	DWORD Index, DoIndex = 0xFFFFFFFF;
	PSPAWNINFO pChar = (PSPAWNINFO)pLocalPlayer;

	for (Index = 0; Index < 10; Index++) {
		if (EQADDR_DOABILITYLIST[Index] != 0xFFFFFFFF) {
			if (!_strnicmp(szBuffer, szSkills[EQADDR_DOABILITYLIST[Index]], strlen(szSkills[EQADDR_DOABILITYLIST[Index]]))) {
				if (Index < 4) {
					DoIndex = Index + 7; // 0-3 = Combat abilities (7-10)
				}
				else {
					DoIndex = Index - 3; // 4-9 = Abilities (1-6)
				}
			}
		}
	}

	if (DoIndex != 0xFFFFFFFF) {
		_itoa_s(DoIndex, szBuffer, 10);
		cmdDoAbility(pChar, szBuffer);
	}
	else {
		if (CHARINFO2 * pChar2 = GetCharInfo2()) {
			for (Index = 0; Index < NUM_COMBAT_ABILITIES; Index++) {
				if (pCombatSkillsSelectWnd->ShouldDisplayThisSkill(Index)) {
					if (PSPELL pCA = GetSpellByID(pChar2->CombatAbilities[Index])) {
						if (!_stricmp(pCA->Name, szBuffer)) {
							//We got the cookie, let's try and do it 
							pCharData->DoCombatAbility(pCA->ID);
							break;
						}
					}
				}
			}
		}
		if (Index >= NUM_COMBAT_ABILITIES)
			WriteChatColor("You do not seem to have that ability available", USERCOLOR_DEFAULT);
	}
	return;
}

// Function to check if the account has a given expansion enabled.
// Pass exansion macros from EQData.h to it -- e.g. HasExpansion(EXPANSION_RoF)
bool HasExpansion(DWORD nExpansion)
{
	return (bool)((GetCharInfo()->ExpansionFlags & nExpansion) != 0);
}

// Just a Function that needs more work
// I use this to test merc aa struct -eqmule
void ListMercAltAbilities()
{
	if (pMercAltAbilities)
	{
		int mercaapoints = ((PCHARINFO)pCharData)->MercAAPoints;

		for (int i = 0; i < MERC_ALT_ABILITY_COUNT; i++)
		{
			PEQMERCALTABILITIES pinfo = (PEQMERCALTABILITIES)pMercAltAbilities;
			if (pinfo->MercAAInfo[i])
			{
				if (pinfo->MercAAInfo[i]->Ptr)
				{
					int nName = pinfo->MercAAInfo[i]->Ptr->nName;
					int maxpoints = pinfo->MercAAInfo[i]->Max;

					if (nName)
					{
						WriteChatf("You have %d mercaapoints to spend on %s (max is %d)",
							mercaapoints, pCDBStr->GetString(nName, eMercenaryAbilityName), maxpoints);
					}
				}
			}
		}
	}
}

CONTENTS* FindItemBySlot2(const ItemGlobalIndex& idx)
{
	return FindItemBySlot(idx.GetTopSlot(), idx.GetIndex().GetSlot(1), idx.GetLocation());
}

CONTENTS* FindItemBySlot(short InvSlot, short BagSlot, ItemContainerInstance location)
{
	CHARINFO2* pChar2 = GetCharInfo2();
	if (location == eItemContainerPossessions) {
		//check regular inventory
		if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray) {
			for (unsigned long nSlot = 0; nSlot < NUM_INV_SLOTS; nSlot++) {
				if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[nSlot]) {
					if (pItem->GetGlobalIndex().GetTopSlot() == InvSlot && pItem->GetGlobalIndex().GetIndex().GetSlot(1) == BagSlot) {
						return pItem;
					}
				}
			}
		}
		//not found? ok check inside bags
		if (pChar2 && pChar2->pInventoryArray) {
			for (unsigned long nPack = 0; nPack < 10; nPack++) {
				if (CONTENTS * pPack = pChar2->pInventoryArray->Inventory.Pack[nPack]) {
					if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
						for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
							if (CONTENTS * pItem = pPack->GetContent(nItem)) {
								if (pItem->GetGlobalIndex().GetTopSlot() == InvSlot && pItem->GetGlobalIndex().GetIndex().GetSlot(1) == BagSlot) {
									return pItem;
								}
							}
						}
					}
				}
			}
		}
	}
	else if (location == eItemContainerBank) {
		//check bank
		CHARINFO* pChar = GetCharInfo();
#ifdef NEWCHARINFO
		if (pChar) {
			for (unsigned long nSlot = 0; nSlot < pChar->BankItems.Items.Size; nSlot++) {
				if (CONTENTS * pItem = pChar->BankItems.Items[nSlot].pObject) {
#else
		if (pChar && pChar->pBankArray && pChar->pBankArray->Bank) {
			for (unsigned long nSlot = 0; nSlot < NUM_BANK_SLOTS; nSlot++) {
				if (CONTENTS * pItem = pChar->pBankArray->Bank[nSlot]) {
#endif
					if (pItem->GetGlobalIndex().GetTopSlot() == InvSlot && pItem->GetGlobalIndex().GetIndex().GetSlot(1) == BagSlot) {
						return pItem;
					}
				}
			}
		}
		//not found? ok check inside bank bags
#ifdef NEWCHARINFO
		if (pChar) {
			for (unsigned long nPack = 0; nPack < pChar->BankItems.Items.Size; nPack++) {
				if (CONTENTS * pPack = pChar->BankItems.Items[nPack].pObject) {
#else
		if (pChar && pChar->pBankArray && pChar->pBankArray->Bank) {
			for (unsigned long nPack = 0; nPack < NUM_BANK_SLOTS; nPack++) {
				if (CONTENTS * pPack = pChar->pBankArray->Bank[nPack]) {
#endif
					if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
						for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
							if (CONTENTS * pItem = pPack->GetContent(nItem)) {
								if (pItem->GetGlobalIndex().GetTopSlot() == InvSlot && pItem->GetGlobalIndex().GetIndex().GetSlot(1) == BagSlot) {
									return pItem;
								}
							}
						}
					}
				}
			}
		}
	}
	else if (location == eItemContainerSharedBank) {
		CHARINFO* pChar = GetCharInfo();
		//what? still not found? ok fine, check shared bank
#ifdef NEWCHARINFO
		if (pChar) {
			for (unsigned long nSlot = 0; nSlot < pChar->SharedBankItems.Items.Size; nSlot++) {
				if (CONTENTS * pItem = pChar->SharedBankItems.Items[nSlot].pObject) {
#else
		if (pChar && pChar->pSharedBankArray && pChar->pSharedBankArray->SharedBank) {
			for (unsigned long nSlot = 0; nSlot < NUM_SHAREDBANK_SLOTS; nSlot++) {
				if (CONTENTS * pItem = pChar->pSharedBankArray->SharedBank[nSlot]) {
#endif
					if (pItem->GetGlobalIndex().GetTopSlot() == InvSlot && pItem->GetGlobalIndex().GetIndex().GetSlot(1) == BagSlot) {
						return pItem;
					}
				}
			}
		}
		//not found? ok check inside sharedbank bags
#ifdef NEWCHARINFO
		if (pChar) {
			for (unsigned long nPack = 0; nPack < pChar->SharedBankItems.Items.Size; nPack++) {
				if (CONTENTS * pPack = pChar->SharedBankItems.Items[nPack].pObject) {
#else
		if (pChar && pChar->pSharedBankArray && pChar->pSharedBankArray->SharedBank) {
			for (unsigned long nPack = 0; nPack < NUM_SHAREDBANK_SLOTS; nPack++) {
				if (CONTENTS * pPack = pChar->pSharedBankArray->SharedBank[nPack]) {
#endif
					if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
						for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
							if (CONTENTS * pItem = pPack->GetContent(nItem)) {
								if (pItem->GetGlobalIndex().GetTopSlot() == InvSlot && pItem->GetGlobalIndex().GetIndex().GetSlot(1) == BagSlot) {
									return pItem;
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}

CONTENTS* FindItemByName(const char* pName, bool bExact)
{
	char Name[MAX_STRING] = { 0 };
	char Temp[MAX_STRING] = { 0 };

	strcpy_s(Name, pName);
	_strlwr_s(Name);

	CHARINFO2* pChar2 = GetCharInfo2();

	// check cursor
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor) {
		if (CONTENTS * pItem = pChar2->pInventoryArray->Inventory.Cursor) {
			if (bExact) {
				if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
					return pItem;
				}
			}
			else {
				strcpy_s(Temp, GetItemFromContents(pItem)->Name);
				_strlwr_s(Temp);
				if (strstr(Temp, Name)) {
					return pItem;
				}
			}
			if (GetItemFromContents(pItem)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
								if (bExact) {
									if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
										return pAugItem;
									}
								}
								else {
									strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
									_strlwr_s(Temp);
									if (strstr(Temp, Name)) {
										return pAugItem;
									}
								}
							}
						}
					}
				}
			}
			else if (pItem->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
				CONTENTS* pPack = pItem;
				for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
					if (CONTENTS * pItem = pPack->GetContent(nItem)) {
						if (bExact) {
							if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
								return pItem;
							}
						}
						else {
							strcpy_s(Temp, GetItemFromContents(pItem)->Name);
							_strlwr_s(Temp);
							if (strstr(Temp, Name)) {
								return pItem;
							}
						}
						// Check for augs next
						if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
							for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
								if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
									if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
										if (bExact) {
											if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
												return pAugItem;
											}
										}
										else {
											strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
											_strlwr_s(Temp);
											if (strstr(Temp, Name)) {
												return pAugItem;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}

	}

	//check toplevel slots
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray) {
		for (unsigned long nSlot = 0; nSlot < NUM_INV_SLOTS; nSlot++) {
			if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[nSlot]) {
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						return pItem;
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						return pItem;
					}
				}
				// Check for augs next
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
								if (bExact) {
									if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
										return pAugItem;
									}
								}
								else {
									strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
									_strlwr_s(Temp);
									if (strstr(Temp, Name)) {
										return pAugItem;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//check the bags
	if (pChar2 && pChar2->pInventoryArray) {
		for (unsigned long nPack = 0; nPack < 10; nPack++) {
			if (CONTENTS * pPack = pChar2->pInventoryArray->Inventory.Pack[nPack]) {
				if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (bExact) {
								if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
									return pItem;
								}
							}
							else {
								strcpy_s(Temp, GetItemFromContents(pItem)->Name);
								_strlwr_s(Temp);
								if (strstr(Temp, Name)) {
									return pItem;
								}
							}
							// We should check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (bExact) {
												if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
													return pAugItem;
												}
											}
											else {
												strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
												_strlwr_s(Temp);
												if (strstr(Temp, Name)) {
													return pAugItem;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//still not found? fine... check mount keyring
	CHARINFO* pChar = GetCharInfo();
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->MountKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->MountKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pMountsArray && pChar->pMountsArray->Mounts) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pMountsArray->Mounts[nSlot]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						return pItem;
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						return pItem;
					}
				}
			}
		}
	}

	//still not found? fine... check illusions keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->IllusionKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->IllusionKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pIllusionsArray && pChar->pIllusionsArray->Illusions) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pIllusionsArray->Illusions[nSlot]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						return pItem;
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						return pItem;
					}
				}
			}
		}
	}

	//still not found? fine... check familiars keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->FamiliarKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->FamiliarKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pFamiliarArray && pChar->pFamiliarArray->Familiars) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pFamiliarArray->Familiars[nSlot]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						return pItem;
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						return pItem;
					}
				}
			}
		}
	}

	return 0;
}
CONTENTS* FindItemByID(int ItemID)
{
	CHARINFO2* pChar2 = GetCharInfo2();

	//check cursor
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor) {
		if (CONTENTS * pItem = pChar2->pInventoryArray->Inventory.Cursor) {
			if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
				return pItem;
			}
			if (GetItemFromContents(pItem)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType && ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
								return pAugItem;
							}
						}
					}
				}
			}
			else if (pItem->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
				CONTENTS* pPack = pItem;
				for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
					if (CONTENTS * pItem = pPack->GetContent(nItem)) {
						if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
							return pItem;
						}
						// Check for augs next
						if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
							for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
								if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
									if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType && ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
										return pAugItem;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//check toplevel slots
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray) {
		for (unsigned long nSlot = 0; nSlot < NUM_INV_SLOTS; nSlot++) {
			if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[nSlot]) {
				if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
					return pItem;
				}
				// Check for augs next
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType && ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
								return pAugItem;
							}
						}
					}
				}
			}
		}
	}

	//check the bags
	if (pChar2 && pChar2->pInventoryArray) {
		for (unsigned long nPack = 0; nPack < 10; nPack++) {
			if (CONTENTS * pPack = pChar2->pInventoryArray->Inventory.Pack[nPack]) {
				if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
								return pItem;
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType && ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
											return pAugItem;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	CHARINFO* pChar = GetCharInfo();
	//still not found? fine... check mount keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->MountKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->MountKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pMountsArray && pChar->pMountsArray->Mounts) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pMountsArray->Mounts[nSlot]) {
#endif
				if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
					return pItem;
				}
			}
		}
	}

	//still not found? fine... check illusions keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->IllusionKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->IllusionKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pIllusionsArray && pChar->pIllusionsArray->Illusions) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pIllusionsArray->Illusions[nSlot]) {
#endif
				if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
					return pItem;
				}
			}
		}
	}

	//still not found? fine... check familiars keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->FamiliarKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->FamiliarKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pFamiliarArray && pChar->pFamiliarArray->Familiars) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pFamiliarArray->Familiars[nSlot]) {
#endif
				if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
					return pItem;
				}
			}
		}
	}

	return 0;
}

int FindItemCountByName(const char* pName, bool bExact)
{

	char Name[MAX_STRING] = { 0 };
	strcpy_s(Name, pName);
	_strlwr_s(Name);
	CHARINFO2* pChar2 = GetCharInfo2();

	char Temp[MAX_STRING] = { 0 };
	int Count = 0;

	// check cursor
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor) {
		if (CONTENTS * pItem = pChar2->pInventoryArray->Inventory.Cursor) {
			if (bExact) {
				if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
					if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pItem->StackCount;
					}
				}
			}
			else {
				strcpy_s(Temp, GetItemFromContents(pItem)->Name);
				_strlwr_s(Temp);
				if (strstr(Temp, Name)) {
					if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pItem->StackCount;
					}
				}
			}
			if (GetItemFromContents(pItem)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
								if (bExact) {
									if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
										Count++;
									}
								}
								else {
									strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
									_strlwr_s(Temp);
									if (strstr(Temp, Name)) {
										Count++;
									}
								}
							}
						}
					}
				}
			}
			else if (pItem->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
				CONTENTS* pPack = pItem;
				for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
					if (CONTENTS * pItem = pPack->GetContent(nItem)) {
						if (bExact) {
							if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
								if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
									Count++;
								}
								else {
									Count += pItem->StackCount;
								}
							}
						}
						else {
							strcpy_s(Temp, GetItemFromContents(pItem)->Name);
							_strlwr_s(Temp);
							if (strstr(Temp, Name)) {
								if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
									Count++;
								}
								else {
									Count += pItem->StackCount;
								}
							}
						}
						// Check for augs next
						if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
							for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
								if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
									if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
										if (bExact) {
											if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
												Count++;
											}
										}
										else {
											strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
											_strlwr_s(Temp);
											if (strstr(Temp, Name)) {
												Count++;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}

	}

	//check toplevel slots
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray) {
		for (unsigned long nSlot = 0; nSlot < NUM_INV_SLOTS; nSlot++) {
			if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[nSlot]) {
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
				// Check for augs next
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
								if (bExact) {
									if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
										Count++;
									}
								}
								else {
									strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
									_strlwr_s(Temp);
									if (strstr(Temp, Name)) {
										Count++;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//check the bags
	if (pChar2 && pChar2->pInventoryArray) {
		for (unsigned long nPack = 0; nPack < 10; nPack++) {
			if (CONTENTS * pPack = pChar2->pInventoryArray->Inventory.Pack[nPack]) {
				if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (bExact) {
								if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
									if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
										Count++;
									}
									else {
										Count += pItem->StackCount;
									}
								}
							}
							else {
								strcpy_s(Temp, GetItemFromContents(pItem)->Name);
								_strlwr_s(Temp);
								if (strstr(Temp, Name)) {
									if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
										Count++;
									}
									else {
										Count += pItem->StackCount;
									}
								}
							}
							// We should check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (bExact) {
												if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
													Count++;
												}
											}
											else {
												strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
												_strlwr_s(Temp);
												if (strstr(Temp, Name)) {
													Count++;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//still not found? fine... check mount keyring
	PCHARINFO pChar = GetCharInfo();
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->MountKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->MountKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pMountsArray && pChar->pMountsArray->Mounts) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pMountsArray->Mounts[nSlot]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
			}
		}
	}

	//still not found? fine... check illusions keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->IllusionKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->IllusionKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pIllusionsArray && pChar->pIllusionsArray->Illusions) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pIllusionsArray->Illusions[nSlot]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
			}
		}
	}

	//still not found? fine... check familiars keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->FamiliarKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->FamiliarKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pFamiliarArray && pChar->pFamiliarArray->Familiars) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pFamiliarArray->Familiars[nSlot]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pItem)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pItem->StackCount;
						}
					}
				}
			}
		}
	}

	return Count;
}
DWORD FindItemCountByID(int ItemID)
{
	DWORD Count = 0;
	//char Name[MAX_STRING] = { 0 };
	char Temp[MAX_STRING] = { 0 };

	CHARINFO2* pChar2 = GetCharInfo2();

	//check cursor
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor) {
		if (CONTENTS * pItem = pChar2->pInventoryArray->Inventory.Cursor) {
			if (ItemID == pItem->ID) {
				if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
					Count++;
				}
				else {
					Count += pItem->StackCount;
				}
			}
			if (GetItemFromContents(pItem)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
								if (ItemID == pAugItem->ID) {
									Count++;
								}
							}
						}
					}
				}
			}
			else if (pItem->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
				CONTENTS* pPack = pItem;
				for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
					if (CONTENTS * pItem = pPack->GetContent(nItem)) {
						if (ItemID == pItem->ID) {
							if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
								Count++;
							}
							else {
								Count += pItem->StackCount;
							}
						}
						// Check for augs next
						if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
							for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
								if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
									if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
										if (ItemID == pAugItem->ID) {
											Count++;
										}
									}
								}
							}
						}
					}
				}
			}
		}

	}

	//check toplevel slots
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray) {
		for (unsigned long nSlot = 0; nSlot < NUM_INV_SLOTS; nSlot++) {
			if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[nSlot]) {
				if (ItemID == pItem->ID) {
					if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pItem->StackCount;
					}
				}
				// Check for augs next
				if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
					for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
						if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
							if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
								if (ItemID == pAugItem->ID) {
									Count++;
								}
							}
						}
					}
				}
			}
		}
	}

	//check the bags
	if (pChar2 && pChar2->pInventoryArray) {
		for (unsigned long nPack = 0; nPack < 10; nPack++) {
			if (CONTENTS * pPack = pChar2->pInventoryArray->Inventory.Pack[nPack]) {
				if (GetItemFromContents(pPack)->Type == ITEMTYPE_PACK && pPack->Contents.ContainedItems.pItems) {
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (ItemID == pItem->ID) {
								if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
									Count++;
								}
								else {
									Count += pItem->StackCount;
								}
							}
							// We should check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (ItemID == pAugItem->ID) {
												Count++;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//still not found? fine... check mount keyring
	PCHARINFO pChar = GetCharInfo();
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->MountKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->MountKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pMountsArray && pChar->pMountsArray->Mounts) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pMountsArray->Mounts[nSlot]) {
#endif
				if (ItemID == pItem->ID) {
					if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pItem->StackCount;
					}
				}
			}
		}
	}

	//still not found? fine... check illusions keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->IllusionKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->IllusionKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pIllusionsArray && pChar->pIllusionsArray->Illusions) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pIllusionsArray->Illusions[nSlot]) {
#endif
				if (ItemID == pItem->ID) {
					if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pItem->StackCount;
					}
				}
			}
		}
	}

	//still not found? fine... check familiars keyring
#ifdef NEWCHARINFO
	if (pChar) {
		for (unsigned long nSlot = 0; nSlot < pChar->FamiliarKeyRingItems.Items.Size; nSlot++) {
			if (CONTENTS * pItem = pChar->FamiliarKeyRingItems.Items[nSlot].pObject) {
#else
	if (pChar && pChar->pFamiliarArray && pChar->pFamiliarArray->Familiars) {
		for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++) {
			if (CONTENTS * pItem = pChar->pFamiliarArray->Familiars[nSlot]) {
#endif
				if (ItemID == pItem->ID) {
					if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pItem->StackCount;
					}
				}
			}
		}
	}

	return Count;
}

CONTENTS* FindBankItemByName(const char* pName, bool bExact)
{
	CHARINFO* pCharInfo = GetCharInfo();
	if (!pCharInfo)
		return nullptr;

	char Name[MAX_STRING] = { 0 };
	strcpy_s(Name, pName);
	_strlwr_s(Name);

	char Temp[MAX_STRING] = { 0 };

	auto checkItem = [&](CONTENTS* pContents) -> bool
	{
		ITEMINFO* pItem = GetItemFromContents(pContents);
		const char* itemName = pItem->Name;

		if (bExact)
		{
			if (!_stricmp(Name, itemName))
				return true;
		}
		else
		{
			if (ci_find_substr(itemName, Name) != -1)
				return true;
		}

		return false;
	};

	auto checkAugs = [&](CONTENTS* pContents) -> CONTENTS *
	{
		if (pContents->Contents.ContainedItems.pItems && pContents->Contents.ContainedItems.Size)
		{
			for (size_t nAug = 0; nAug < pContents->Contents.ContainedItems.Size; nAug++)
			{
				if (CONTENTS * pAugItem = pContents->Contents.ContainedItems.pItems->Item[nAug])
				{
					ITEMINFO* pItem = GetItemFromContents(pAugItem);
					if (pItem->Type == ITEMTYPE_NORMAL && pItem->AugType)
					{
						if (checkItem(pAugItem))
							return pAugItem;
					}
				}
			}
		}

		return nullptr;
	};

	auto checkContainer = [&](CONTENTS* pPack) -> CONTENTS *
	{
		// check this item
		if (checkItem(pPack))
			return pPack;

		if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK)
		{
			// Hey it's not a pack we should check for augs
			if (CONTENTS * pAugItem = checkAugs(pPack))
				return pAugItem;
		}
		else if (pPack->Contents.ContainedItems.pItems)
		{
			// Ok it was a pack, if it has items in it lets check them
			for (int nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++)
			{
				if (CONTENTS * pItem = pPack->GetContent(nItem))
				{
					// check this item
					if (checkItem(pItem))
						return pItem;

					// Check for augs next
					if (CONTENTS * pAugItem = checkAugs(pItem))
						return pAugItem;
				}
			}
		}

		return nullptr;
	};

	// Check bank slots
	if (pCharInfo->pBankArray && pCharInfo->pBankArray->Bank)
	{
		for (int nPack = 0; nPack < NUM_BANK_SLOTS; nPack++)
		{
			CONTENTS* pPack = pCharInfo->pBankArray->Bank[nPack];
			if (!pPack)
				continue;

			if (CONTENTS * pItem = checkContainer(pPack))
				return pItem;
		}
	}

	// Check shared bank slots
	if (pCharInfo->pSharedBankArray)
	{
		for (int nPack = 0; nPack < NUM_SHAREDBANK_SLOTS; nPack++)
		{
			CONTENTS* pPack = pCharInfo->pSharedBankArray->SharedBank[nPack];
			if (!pPack)
				continue;

			if (CONTENTS * pItem = checkContainer(pPack))
				return pItem;
		}
	}

	return nullptr;
}

CONTENTS* FindBankItemByID(int ItemID)
{
	PCHARINFO pCharInfo = GetCharInfo();

	// Check bank slots
#ifdef NEWCHARINFO
	if (pCharInfo) {
		for (unsigned long nPack = 0; nPack < pCharInfo->BankItems.Items.Size; nPack++) {
			if (CONTENTS * pPack = pCharInfo->BankItems.Items[nPack].pObject) {
#else
	if (pCharInfo && pCharInfo->pBankArray && pCharInfo->pBankArray->Bank) {
		for (unsigned long nPack = 0; nPack < NUM_BANK_SLOTS; nPack++) {
			if (CONTENTS * pPack = pCharInfo->pBankArray->Bank[nPack]) {
#endif
				if (ItemID == GetItemFromContents(pPack)->ItemNumber) {
					return pPack;
				}
				if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
					if (pPack->Contents.ContainedItems.pItems && pPack->Contents.ContainedItems.Size) {
						for (unsigned long nAug = 0; nAug < pPack->Contents.ContainedItems.Size; nAug++) {
							if (CONTENTS * pAugItem = pPack->Contents.ContainedItems.pItems->Item[nAug]) {
								if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
									if (ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
										return pAugItem;
									}
								}
							}
						}
					}
				}
				else if (pPack->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
								return pItem;
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (ItemID == GetItemFromContents(pItem)->ItemNumber) {
												return pItem;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Check shared bank slots
#ifdef NEWCHARINFO
	if (pCharInfo) {
		for (unsigned long nPack = 0; nPack < pCharInfo->SharedBankItems.Items.Size; nPack++) {
			if (CONTENTS * pPack = pCharInfo->SharedBankItems.Items[nPack].pObject) {
#else
	if (pCharInfo->pSharedBankArray) {
		for (unsigned long nPack = 0; nPack < NUM_SHAREDBANK_SLOTS; nPack++) {
			if (CONTENTS * pPack = pCharInfo->pSharedBankArray->SharedBank[nPack]) {
#endif
				if (ItemID == GetItemFromContents(pPack)->ItemNumber) {
					return pPack;
				}
				if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
					if (pPack->Contents.ContainedItems.pItems && pPack->Contents.ContainedItems.Size) {
						for (unsigned long nAug = 0; nAug < pPack->Contents.ContainedItems.Size; nAug++) {
							if (CONTENTS * pAugItem = pPack->Contents.ContainedItems.pItems->Item[nAug]) {
								if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
									if (ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
										return pAugItem;
									}
								}
							}
						}
					}
				}
				else if (pPack->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (ItemID == GetItemFromContents(pPack)->ItemNumber) {
								return pPack;
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (ItemID == GetItemFromContents(pAugItem)->ItemNumber) {
												return pAugItem;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return NULL;
}

int FindBankItemCountByName(const char* pName, bool bExact)
{
	DWORD Count = 0;
	char Name[MAX_STRING] = { 0 };
	char Temp[MAX_STRING] = { 0 };
	strcpy_s(Name, pName);
	_strlwr_s(Name);
	PCHARINFO pCharInfo = GetCharInfo();

	// Check bank slots
#ifdef NEWCHARINFO
	if (pCharInfo) {
		for (unsigned long nPack = 0; nPack < pCharInfo->BankItems.Items.Size; nPack++) {
			if (CONTENTS * pPack = pCharInfo->BankItems.Items[nPack].pObject) {
#else
	if (pCharInfo && pCharInfo->pBankArray && pCharInfo->pBankArray->Bank) {
		for (unsigned long nPack = 0; nPack < NUM_BANK_SLOTS; nPack++) {
			if (CONTENTS * pPack = pCharInfo->pBankArray->Bank[nPack]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pPack)->Name)) {
						if ((GetItemFromContents(pPack)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pPack)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pPack->StackCount;
						}
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pPack)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						if ((GetItemFromContents(pPack)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pPack)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pPack->StackCount;
						}
					}
				}
				if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
					if (pPack->Contents.ContainedItems.pItems && pPack->Contents.ContainedItems.Size) {
						for (unsigned long nAug = 0; nAug < pPack->Contents.ContainedItems.Size; nAug++) {
							if (CONTENTS * pAugItem = pPack->Contents.ContainedItems.pItems->Item[nAug]) {
								if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
									if (bExact) {
										if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
											Count++;
										}
									}
									else {
										strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
										_strlwr_s(Temp);
										if (strstr(Temp, Name)) {
											Count++;
										}
									}
								}
							}
						}
					}
				}
				else if (pPack->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (bExact) {
								if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
									if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
										Count++;
									}
									else {
										Count += pItem->StackCount;
									}
								}
							}
							else {
								strcpy_s(Temp, GetItemFromContents(pItem)->Name);
								_strlwr_s(Temp);
								if (strstr(Temp, Name)) {
									if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
										Count++;
									}
									else {
										Count += pItem->StackCount;
									}
								}
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (bExact) {
												if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
													Count++;
												}
											}
											else {
												strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
												_strlwr_s(Temp);
												if (strstr(Temp, Name)) {
													Count++;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Check shared bank slots
#ifdef NEWCHARINFO
	if (pCharInfo) {
		for (unsigned long nPack = 0; nPack < pCharInfo->SharedBankItems.Items.Size; nPack++) {
			if (CONTENTS * pPack = pCharInfo->SharedBankItems.Items[nPack].pObject) {
#else
	if (pCharInfo->pSharedBankArray) {
		for (unsigned long nPack = 0; nPack < NUM_SHAREDBANK_SLOTS; nPack++) {
			if (CONTENTS * pPack = pCharInfo->pSharedBankArray->SharedBank[nPack]) {
#endif
				if (bExact) {
					if (!_stricmp(Name, GetItemFromContents(pPack)->Name)) {
						if ((GetItemFromContents(pPack)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pPack)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pPack->StackCount;
						}
					}
				}
				else {
					strcpy_s(Temp, GetItemFromContents(pPack)->Name);
					_strlwr_s(Temp);
					if (strstr(Temp, Name)) {
						if ((GetItemFromContents(pPack)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pPack)->IsStackable() != 1)) {
							Count++;
						}
						else {
							Count += pPack->StackCount;
						}
					}
				}
				if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
					if (pPack->Contents.ContainedItems.pItems && pPack->Contents.ContainedItems.Size) {
						for (unsigned long nAug = 0; nAug < pPack->Contents.ContainedItems.Size; nAug++) {
							if (CONTENTS * pAugItem = pPack->Contents.ContainedItems.pItems->Item[nAug]) {
								if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
									if (bExact) {
										if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
											Count++;
										}
									}
									else {
										strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
										_strlwr_s(Temp);
										if (strstr(Temp, Name)) {
											Count++;
										}
									}
								}
							}
						}
					}
				}
				else if (pPack->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (bExact) {
								if (!_stricmp(Name, GetItemFromContents(pItem)->Name)) {
									if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
										Count++;
									}
									else {
										Count += pItem->StackCount;
									}
								}
							}
							else {
								strcpy_s(Temp, GetItemFromContents(pItem)->Name);
								_strlwr_s(Temp);
								if (strstr(Temp, Name)) {
									if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
										Count++;
									}
									else {
										Count += pItem->StackCount;
									}
								}
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (bExact) {
												if (!_stricmp(Name, GetItemFromContents(pAugItem)->Name)) {
													Count++;
												}
											}
											else {
												strcpy_s(Temp, GetItemFromContents(pAugItem)->Name);
												_strlwr_s(Temp);
												if (strstr(Temp, Name)) {
													Count++;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return Count;
}

int FindBankItemCountByID(int ItemID)
{
	DWORD Count = 0;
	char Temp[MAX_STRING] = { 0 };
	PCHARINFO pCharInfo = GetCharInfo();

	// Check bank slots
#ifdef NEWCHARINFO
	if (pCharInfo) {
		for (unsigned long nPack = 0; nPack < pCharInfo->BankItems.Items.Size; nPack++) {
			if (CONTENTS * pPack = pCharInfo->BankItems.Items[nPack].pObject) {
#else
	if (pCharInfo && pCharInfo->pBankArray && pCharInfo->pBankArray->Bank) {
		for (unsigned long nPack = 0; nPack < NUM_BANK_SLOTS; nPack++) {
			if (CONTENTS * pPack = pCharInfo->pBankArray->Bank[nPack]) {
#endif
				if (ItemID == pPack->ID) {
					if ((GetItemFromContents(pPack)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pPack)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pPack->StackCount;
					}
				}
				if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
					if (pPack->Contents.ContainedItems.pItems && pPack->Contents.ContainedItems.Size) {
						for (unsigned long nAug = 0; nAug < pPack->Contents.ContainedItems.Size; nAug++) {
							if (CONTENTS * pAugItem = pPack->Contents.ContainedItems.pItems->Item[nAug]) {
								if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
									if (ItemID == pAugItem->ID) {
										Count++;
									}
								}
							}
						}
					}
				}
				else if (pPack->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (ItemID == pItem->ID) {
								if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
									Count++;
								}
								else {
									Count += pItem->StackCount;
								}
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (ItemID == pAugItem->ID) {
												Count++;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Check shared bank slots
#ifdef NEWCHARINFO
	if (pCharInfo) {
		for (unsigned long nPack = 0; nPack < pCharInfo->SharedBankItems.Items.Size; nPack++) {
			if (CONTENTS * pPack = pCharInfo->SharedBankItems.Items[nPack].pObject) {
#else
	if (pCharInfo->pSharedBankArray) {
		for (unsigned long nPack = 0; nPack < NUM_SHAREDBANK_SLOTS; nPack++) {
			if (CONTENTS * pPack = pCharInfo->pSharedBankArray->SharedBank[nPack]) {
#endif
				if (ItemID == pPack->ID) {
					if ((GetItemFromContents(pPack)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pPack)->IsStackable() != 1)) {
						Count++;
					}
					else {
						Count += pPack->StackCount;
					}
				}
				if (GetItemFromContents(pPack)->Type != ITEMTYPE_PACK) { // Hey it's not a pack we should check for augs
					if (pPack->Contents.ContainedItems.pItems && pPack->Contents.ContainedItems.Size) {
						for (unsigned long nAug = 0; nAug < pPack->Contents.ContainedItems.Size; nAug++) {
							if (CONTENTS * pAugItem = pPack->Contents.ContainedItems.pItems->Item[nAug]) {
								if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
									if (ItemID == pAugItem->ID) {
										Count++;
									}
								}
							}
						}
					}
				}
				else if (pPack->Contents.ContainedItems.pItems) { // Ok it was a pack, if it has items in it lets check them
					for (unsigned long nItem = 0; nItem < GetItemFromContents(pPack)->Slots; nItem++) {
						if (CONTENTS * pItem = pPack->GetContent(nItem)) {
							if (ItemID == pItem->ID) {
								if ((GetItemFromContents(pItem)->Type != ITEMTYPE_NORMAL) || (((EQ_Item*)pItem)->IsStackable() != 1)) {
									Count++;
								}
								else {
									Count += pItem->StackCount;
								}
							}
							// Check for augs next
							if (pItem->Contents.ContainedItems.pItems && pItem->Contents.ContainedItems.Size) {
								for (unsigned long nAug = 0; nAug < pItem->Contents.ContainedItems.Size; nAug++) {
									if (CONTENTS * pAugItem = pItem->Contents.ContainedItems.pItems->Item[nAug]) {
										if (GetItemFromContents(pAugItem)->Type == ITEMTYPE_NORMAL && GetItemFromContents(pAugItem)->AugType) {
											if (ItemID == pAugItem->ID) {
												Count++;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return Count;
}

CInvSlot* GetInvSlot2(const ItemGlobalIndex & index)
{
	return GetInvSlot(index.Location, index.GetIndex().GetSlot(0), index.GetIndex().GetSlot(1));
}

CInvSlot* GetInvSlot(DWORD type, short invslot, short bagslot)
{
	if (pInvSlotMgr)
	{
		for (int i = 0; i < pInvSlotMgr->TotalSlots; i++)
		{
			CInvSlot* pSlot = pInvSlotMgr->SlotArray[i];

			if (pSlot && pSlot->bEnabled && pSlot->pInvSlotWnd
				&& pSlot->pInvSlotWnd->ItemLocation.GetLocation() == type
				&& (short)pSlot->pInvSlotWnd->ItemLocation.GetIndex().GetSlot(0) == invslot
				&& (short)pSlot->pInvSlotWnd->ItemLocation.GetIndex().GetSlot(1) == bagslot)
			{
				if (CXMLData * pXMLData = pSlot->pInvSlotWnd->GetXMLData())
				{
					if (!_stricmp(pXMLData->ScreenID.c_str(), "HB_InvSlot"))
					{
						//we dont want this, the user specified a container , not a hotbutton...
						continue;
					}
				}

				return pSlot;
			}
		}
	}

	return nullptr;
}

//work in progress -eqmule
bool IsItemInsideContainer(CONTENTS * pItem)
{
	if (!pItem)
		return false;
	CHARINFO2* pChar2 = GetCharInfo2();
	if (!pChar2)
		return false;

	// TODO: Just check if !IsBaseIndex()
	int index = pItem->GetGlobalIndex().GetTopSlot();

	if (index >= 0 && index <= NUM_INV_SLOTS)
	{
		if (pChar2 && pChar2->pInventoryArray)
		{
			if (CONTENTS * pItemFound = pChar2->pInventoryArray->InventoryArray[index])
			{
				if (pItemFound != pItem)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool OpenContainer(CONTENTS * pItem, bool hidden, bool flag)
{
	if (!pItem)
		return false;

	if (CONTENTS * pcont = FindItemBySlot2(pItem->GetGlobalIndex()))
	{
		if (pcont->Open)
			return true;

		if (GetItemFromContents(pcont)->Type == ITEMTYPE_PACK)
		{
			if (CInvSlot * pSlot = GetInvSlot2(pcont->GetGlobalIndex()))
			{
				if (hidden)
				{
					// put code to hide bag here
					// until i can figure out how to call moveitemqty
				}

				ItemGlobalIndex To = pSlot->pInvSlotWnd->ItemLocation;
				To.Location = pcont->GetGlobalIndex().Location; // eItemContainerPossessions;

				pContainerMgr->OpenContainer(&pcont, To, flag);
				//pPCData->AlertInventoryChanged();
				return pcont->Open;
			}
		}
	}
	return false;
}

bool CloseContainer(CONTENTS * pItem)
{
	if (!pItem)
		return false;

	if (CONTENTS * pcont = FindItemBySlot2(pItem->GetGlobalIndex()))
	{
		if (!pcont->Open)
			return false;

		if (GetItemFromContents(pcont)->Type == ITEMTYPE_PACK)
		{
			pContainerMgr->CloseContainer(&pcont, true);
			return !pcont->Open;
		}
	}

	return false;
}

//WaitForBagToOpen code by eqmule 2014
DWORD __stdcall WaitForBagToOpen(void* pData)
{
	PLARGE_INTEGER i64tmp = (PLARGE_INTEGER)pData;
	ItemContainerInstance type = (ItemContainerInstance)i64tmp->LowPart;
	CONTENTS* pItem = (CONTENTS*)i64tmp->HighPart;
	int timeout = 0;

	if (CONTENTS * pcont = FindItemBySlot2(pItem->GetGlobalIndex()))
	{
		if (pInvSlotMgr)
		{
			if (CInvSlot * theslot = pInvSlotMgr->FindInvSlot(pItem->GetGlobalIndex()))
			{
				if (theslot->pInvSlotWnd)
				{
					while (!theslot->pInvSlotWnd->IsVisible())
					{
						if (GetGameState() != GAMESTATE_INGAME)
							break;

						Sleep(10);
						timeout += 100;
						if (timeout >= 1000)
						{
							break;
						}
					}
				}
			}
		}

		//this is most likely completely useless
		//since the bag will actually always be open at this point
		//how can i check if the item is in the slot?
		//need to look into this further
		//get the texture maybe? -eqmule
		/*while(!pcont->Open) {
		Sleep(10);
		timeout+=100;
		if(timeout>=1000) {
		break;
		}
		}*/
	}

	Sleep(100);

	if (pWndMgr)
	{
		bool Old = pWndMgr->KeyboardFlags[1];
		pWndMgr->KeyboardFlags[1] = 1;

		if (ItemOnCursor())
		{
			DropItem(type, pItem->GetGlobalIndex().GetTopSlot(), pItem->GetGlobalIndex().GetIndex().GetSlot(1));
		}
		else
		{
			PickupItem(type, pItem);
		}

		pWndMgr->KeyboardFlags[1] = Old;
		LocalFree(pData);
		//CloseContainer(pItem);
	}
	return 1;
}

bool ItemOnCursor()
{
	CHARINFO2* pChar2 = GetCharInfo2();
	if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor)
	{
		return true;
	}
	return false;
}

bool PickupItem(ItemContainerInstance type, CONTENTS * pItem)
{
	if (!pItem || !pInvSlotMgr)
	{
		return false;
	}

	bool bSelectSlot = false;

	if (pMerchantWnd && pMerchantWnd->IsVisible()) {
		// if the merchant window is open, we dont actually drop anything we just select the slot
		bSelectSlot = true;
	}

	if (pItem->GetGlobalIndex().GetIndex().IsBase())
	{
		// ok so they want to pick it up from a toplevelslot
		CInvSlot* pSlot = GetInvSlot(type, pItem->GetGlobalIndex().GetTopSlot());
		if (!pSlot || !pSlot->pInvSlotWnd)
		{
			// if we got all the way here this really shouldnt happen... but why assume...
			WriteChatf("Could not find the %d itemslot", pItem->GetGlobalIndex().GetTopSlot());
			return false;
		}

		if (bSelectSlot)
		{
			if (CInvSlot * theslot = pInvSlotMgr->FindInvSlot(pItem->GetGlobalIndex().GetTopSlot()))
			{
				pInvSlotMgr->SelectSlot(theslot);

				ItemGlobalIndex To;
				To.Location = eItemContainerPossessions;
				To.Index.SetSlot(0, pItem->GetGlobalIndex().GetTopSlot());
				To.Index.SetSlot(1, pItem->GetGlobalIndex().GetIndex().GetSlot(1));

				pMerchantWnd->SelectBuySellSlot(To);
				return true;
			}
		}
		else
		{
			// just move it from the slot to the cursor
			ItemGlobalIndex From;
			From.Location = (ItemContainerInstance)pItem->GetGlobalIndex().GetLocation();
			From.Index.SetSlot(0, pItem->GetGlobalIndex().GetTopSlot());

			ItemGlobalIndex To;
			To.Location = eItemContainerPossessions;
			To.Index.SetSlot(0, eItemContainerCursor);

			pInvSlotMgr->MoveItem(From, To, true, true);
			return true;
		}
	}
	else
	{
		// BagSlot is NOT -1 so they want to pick it up from INSIDE a bag
		if (bSelectSlot)
		{
			if (CInvSlot * theslot = pInvSlotMgr->FindInvSlot(pItem->GetGlobalIndex().GetTopSlot(), pItem->GetGlobalIndex().GetIndex().GetSlot(1)))
			{
				pInvSlotMgr->SelectSlot(theslot);
				ItemGlobalIndex To;
				To.Location = eItemContainerPossessions;
				To.Index.SetSlot(0, pItem->GetGlobalIndex().GetIndex().GetSlot(0));
				To.Index.SetSlot(1, pItem->GetGlobalIndex().GetIndex().GetSlot(1));

				pMerchantWnd->SelectBuySellSlot(To);
				return true;
			}
			else
			{
				// well now is where it gets complicated then... or not...
				ItemGlobalIndex To;
				To.Location = eItemContainerPossessions;
				To.Index.SetSlot(0, pItem->GetGlobalIndex().GetIndex().GetSlot(0));
				To.Index.SetSlot(1, pItem->GetGlobalIndex().GetIndex().GetSlot(1));

				pMerchantWnd->SelectBuySellSlot(To);
				return true;
			}
		}
		else
		{
			// not a selected slot
			// ok so its a slot inside a bag
			// is ctrl pressed?
			// if it is we HAVE to open the bag, until I get a bypass worked out

			uint32_t keybflag = pWndMgr->GetKeyboardFlags();

			if (keybflag == 2 && pItem->StackCount > 1)
			{
				// ctrl was pressed and it is a stackable item
				// I need to open the bag and notify it cause moveitem only picks up full stacks
				CInvSlot* pSlot = GetInvSlot2(pItem->GetGlobalIndex());
				if (!pSlot)
				{
					// well lets try to open it then
					if (CONTENTS * pBag = FindItemBySlot2(pItem->GetGlobalIndex().GetParent()))
					{
						bool wechangedpackopenstatus = OpenContainer(pBag, true);
						if (wechangedpackopenstatus)
						{
							if (PLARGE_INTEGER i64tmp = (PLARGE_INTEGER)LocalAlloc(LPTR, sizeof(LARGE_INTEGER)))
							{
								i64tmp->LowPart = type;
								i64tmp->HighPart = (LONG)pItem;
								DWORD nThreadId = 0;
								CreateThread(nullptr, 0, WaitForBagToOpen, i64tmp, 0, &nThreadId);
								return false;
							}
						}
					}
					else
					{
						WriteChatf("[PickupItem] falied due to no bag found in slot %d", pItem->GetGlobalIndex().GetTopSlot());
						return false;
					}
				}
				else
				{
					// ok so the bag is open...
					// well we just select it then...
					if (!pSlot->pInvSlotWnd || !SendWndClick2(pSlot->pInvSlotWnd, "leftmouseup"))
					{
						WriteChatf("Could not pickup %s", GetItemFromContents(pItem)->Name);
					}
					return true;
				}

				// thread this? hmm if i close it before item ends up on cursor, it wont...
				// if(wechangedpackopenstatus)
				//     CloseContainer(pItem);
				return false;
			}
			else
			{
				// ctrl is NOT pressed
				// we can just move the whole stack
				ItemGlobalIndex From;
				From.Location = (ItemContainerInstance)pItem->GetGlobalIndex().Location;
				From.Index.SetSlot(0, pItem->GetGlobalIndex().GetTopSlot());
				From.Index.SetSlot(1, pItem->GetGlobalIndex().GetIndex().GetSlot(1));

				ItemGlobalIndex To;
				To.Location = eItemContainerPossessions;
				To.Index.SetSlot(0, eItemContainerCursor); // this is probably wrong

				CHARINFO2* pChar2 = GetCharInfo2();
				CONTENTS* pContBefore = pChar2->pInventoryArray->Inventory.Cursor;
				pInvSlotMgr->MoveItem(From, To, true, true, false, true);

				if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor)
				{
					CONTENTS* pContAfter = pChar2->pInventoryArray->Inventory.Cursor;

					EqItemGuid g;
					strcpy_s(g.guid, 18, "0000000000000000");

					pCursorAttachment->AttachToCursor(nullptr, nullptr, eCursorAttachment_Item, -1, g, 0, nullptr, nullptr);
				}
				else
				{
					pCursorAttachment->Deactivate();
				}
			}
			return true;
		}
	}
	return false;
}

bool DropItem2(const ItemGlobalIndex & index)
{
	return DropItem(index.GetLocation(), index.GetTopSlot(), index.GetIndex().GetSlot(1));
}

bool DropItem(ItemContainerInstance type, short ToInvSlot, short ToBagSlot)
{
	if (!pInvSlotMgr)
		return false;

	bool bSelectSlot = false;
	if (pMerchantWnd && pMerchantWnd->IsVisible())
	{
		// if the merchant window is open, we dont actually drop anything we just select the slot
		bSelectSlot = true;
	}

	if (ToBagSlot == -1)
	{
		// they want to drop it to a toplevelslot
		CInvSlot* pSlot = GetInvSlot(type, ToInvSlot);
		if (!pSlot || !pSlot->pInvSlotWnd)
		{
			// if we got all the way here this really shouldnt happen... but why assume...
			WriteChatf("Could not find the %d itemslot", ToInvSlot);
			return false;
		}

		if (bSelectSlot)
		{
			if (CInvSlot * theSlot = pInvSlotMgr->FindInvSlot(ToInvSlot))
			{
				// we select the slot, and that will set pSelectedItem correctly
				// we do this cause later on we need that address for the .Selection member
				pInvSlotMgr->SelectSlot(theSlot);

				ItemGlobalIndex To = theSlot->pInvSlotWnd->ItemLocation;
				To.Location = eItemContainerPossessions;

				pMerchantWnd->SelectBuySellSlot(To);
				return true;
			}
		}
		else
		{
			// just move it from cursor to the slot
			ItemGlobalIndex From;
			From.Location = eItemContainerPossessions;
			From.Index.SetSlot(0, eItemContainerCursor);   // TODO: Check this, i'm pretty sure its wrong.

			ItemGlobalIndex To;
			To.Location = type;
			To.Index.SetSlot(0, ToInvSlot);
			To.Index.SetSlot(1, ToBagSlot);

			pInvSlotMgr->MoveItem(From, To, true, true);
			return true;
		}
	}
	else
	{
		// BagSlot is NOT -1 so they want to drop it INSIDE a bag
		if (bSelectSlot)
		{
			if (CInvSlot * theSlot = pInvSlotMgr->FindInvSlot(ToInvSlot, ToBagSlot))
			{
				pInvSlotMgr->SelectSlot(theSlot);

				ItemGlobalIndex To;
				To.Location = eItemContainerPossessions;
				To.Index.SetSlot(0, theSlot->pInvSlotWnd->ItemLocation.GetTopSlot());
				To.Index.SetSlot(1, theSlot->pInvSlotWnd->ItemLocation.GetIndex().GetSlot(1));

				pMerchantWnd->SelectBuySellSlot(To);
				return true;

			}
			else
			{
				// well now is where it gets complicated then...
				// so we need to open the bag...

				ItemGlobalIndex To;
				To.Location = eItemContainerPossessions;
				To.Index.SetSlot(0, ToInvSlot);
				To.Index.SetSlot(1, ToBagSlot);

				pMerchantWnd->SelectBuySellSlot(To);
				return true;
			}
		}
		else
		{
			// ok so its a slot inside a bag
			ItemGlobalIndex From;
			From.Location = eItemContainerPossessions;
			From.Index.SetSlot(0, eItemContainerCursor); // TODO: Check this, i'm pretty sure its wrong.

			ItemGlobalIndex To;
			To.Location = type;
			To.Index.SetSlot(0, ToInvSlot);
			To.Index.SetSlot(1, ToBagSlot);

			CHARINFO2* pChar2 = GetCharInfo2();
			CONTENTS* pContBefore = pChar2->pInventoryArray->Inventory.Cursor;

			pInvSlotMgr->MoveItem(From, To, true, true, true, false);

			if (pChar2 && pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor)
			{
				CONTENTS* pContAfter = pChar2->pInventoryArray->Inventory.Cursor;

				EqItemGuid g;
				strcpy_s(g.guid, 18, "0000000000000000");
				CCursorAttachment* pCursAtch = pCursorAttachment;

				pCursAtch->AttachToCursor(nullptr, nullptr, eCursorAttachment_Item, -1, g, 0, nullptr, nullptr);
			}
			else
			{
				pCursorAttachment->Deactivate();
			}
		}
		return true;
	}
	return false;
}

int GetTargetBuffByCategory(int category, unsigned int classmask, int startslot)
{
	if (pTargetWnd->Type <= 0)
		return false;

	int buffID = 0;
	for (int i = startslot; i < NUM_BUFF_SLOTS; i++)
	{
		buffID = pTargetWnd->BuffSpellID[i];
		if (buffID > 0)
		{
			if (SPELL * pSpell = GetSpellByID(buffID))
			{
				if (GetSpellCategory(pSpell) == category && IsSpellUsableForClass(pSpell, classmask))
				{
					return i;
				}
			}
		}
	}
	return -1;
}

int GetTargetBuffBySubCat(const char* subcat, unsigned int classmask, int startslot)
{
	if (pTargetWnd->Type <= 0)
		return -1;

	for (int i = startslot; i < NUM_BUFF_SLOTS; i++)
	{
		int buffID = pTargetWnd->BuffSpellID[i];
		if (buffID > 0)
		{
			SPELL* pSpell = GetSpellByID(buffID);
			if (!pSpell) continue;

			int cat = GetSpellSubcategory(pSpell);
			if (!cat) continue;

			const char* ptr = pCDBStr->GetString(cat, eSpellCategory);
			if (!ptr) continue;

			if (!_stricmp(ptr, subcat))
			{
				if (classmask == Unknown)
				{
					return i;
				}

				for (int N = 0; N < 16; N++)
				{
					if (classmask & (1 << N))
					{
						return i;
					}
				}
			}
		}
	}

	return -1;
}

bool HasCachedTargetBuffSubCat(const char* subcat, SPAWNINFO * pSpawn, TargetBuff * pcTargetBuff, unsigned int classmask)
{
	if (CachedBuffsMap.empty())
		return false;

	auto i = CachedBuffsMap.find(pSpawn->SpawnID);
	if (i == CachedBuffsMap.end())
		return false;

	for (auto& iter : i->second)
	{
		int buffID = iter.first;
		if (SPELL * pSpell = GetSpellByID(buffID))
		{
			if (int cat = GetSpellSubcategory(pSpell))
			{
				if (const char* ptr = pCDBStr->GetString(cat, eSpellCategory))
				{
					if (!_stricmp(ptr, subcat))
					{
						if (classmask == Unknown)
							return true;

						for (int N = 0; N < 16; N++)
						{
							if (classmask & (1 << N))
							{
								return true;
							}
						}
					}
				}
			}
		}
	}

	return false;
}

bool HasCachedTargetBuffSPA(int spa, bool bIncrease, SPAWNINFO * pSpawn, TargetBuff * pcTargetBuff)
{
	auto i = CachedBuffsMap.find(pSpawn->SpawnID);
	if (i == CachedBuffsMap.end())
		return false;

	for (auto& iter : i->second)
	{
		int buffID = iter.first;

		if (SPELL * pSpell = GetSpellByID(buffID))
		{
			if (LONG base = ((EQ_Spell*)pSpell)->SpellAffectBase(spa))
			{
				strcpy_s(pcTargetBuff->casterName, iter.second.casterName);
				pcTargetBuff->count = iter.second.count;
				pcTargetBuff->duration = iter.second.duration;
				pcTargetBuff->slot = iter.second.slot;
				pcTargetBuff->spellId = iter.second.spellId;
				pcTargetBuff->timeStamp = iter.second.timeStamp;

				switch (spa)
				{
				case 3: // Movement Rate
					if (!bIncrease && base < 0)
					{
						// below 0 means its a snare above its runspeed increase...
						return true;
					}
					else if (bIncrease && base > 0)
					{
						return true;
					}
					return false;

				case 11: // Melee Speed
					if (!bIncrease && base < 100)
					{
						// below 100 means its a slow above its haste...
						return true;
					}
					else if (bIncrease && base > 100)
					{
						return true;
					}
					return false;

				case 59: // Damage Shield
					if (!bIncrease && base > 0)
					{
						// decreased DS
						return true;
					}
					else if (bIncrease && base < 0)
					{
						// increased DS
						return true;
					}
					return false;

				case 121: // Reverse Damage Shield
					if (!bIncrease && base > 0)
					{
						// decreased DS
						return true;
					}
					else if (bIncrease && base < 0)
					{
						// increased DS
						return true;
					}
					return false;

				default:
					return true;
				}
			}
		}
	}

	return false;
}

//Usage: The spa is the spellaffect id, for example 11 for Melee Speed
//       the bIncrease tells the function if we want spells that increase or decrease the SPA
int GetTargetBuffBySPA(int spa, bool bIncrease, int startslot)
{
	if (pTargetWnd->Type <= 0)
		return false;

	int buffID = 0;
	for (int i = startslot; i < NUM_BUFF_SLOTS; i++)
	{
		buffID = pTargetWnd->BuffSpellID[i];
		if (buffID > 0 && buffID != -1)
		{
			if (SPELL * pSpell = GetSpellByID(buffID))
			{
				if (int base = ((EQ_Spell*)pSpell)->SpellAffectBase(spa))
				{
					switch (spa)
					{
					case 3: // Movement Rate
						if (!bIncrease && base < 0)
						{
							// below 0 means its a snare above its runspeed increase...
							return i;
						}
						else if (bIncrease && base > 0)
						{
							return i;
						}
						return -1;

					case 11: // Melee Speed
						if (!bIncrease && base < 100)
						{
							// below 100 means its a slow above its haste...
							return i;
						}
						else if (bIncrease && base > 100)
						{
							return i;
						}
						return -1;

					case 59: // Damage Shield
						if (!bIncrease && base > 0)
						{
							// decreased DS
							return i;
						}
						else if (bIncrease && base < 0)
						{
							// increased DS
							return i;
						}
						return -1;

					case 121: // Reverse Damage Shield
						if (!bIncrease && base > 0)
						{
							// decreased DS
							return i;
						}
						else if (bIncrease && base < 0)
						{
							// increased DS
							return i;
						}
						return -1;

					default:
						return i;
					}
				}
			}
		}
	}
	return -1;
}

int GetSelfBuffByCategory(int category, unsigned int classmask, int startslot)
{
	CHARINFO2* pChar2 = GetCharInfo2();
	if (!pChar2)
		return -1;

	for (int i = startslot; i < NUM_BUFF_SLOTS; i++)
	{
		if (SPELL * pSpell = GetSpellByID(pChar2->Buff[i].SpellID))
		{
			if (GetSpellCategory(pSpell) == category && IsSpellUsableForClass(pSpell, classmask))
			{
				return i;
			}
		}
	}

	return -1;
}

int GetSelfBuffBySubCat(const char* subcat, unsigned int classmask, int startslot)
{
	PcProfile* pProfile = GetPcProfile();
	if (!pProfile)
		return -1;

	for (int i = startslot; i < NUM_LONG_BUFFS; i++)
	{
		if (SPELL * pSpell = GetSpellByID(pProfile->Buff[i].SpellID))
		{
			if (DWORD cat = GetSpellSubcategory(pSpell))
			{
				if (const char* ptr = pCDBStr->GetString(cat, eSpellCategory))
				{
					if (!_stricmp(ptr, subcat) && IsSpellUsableForClass(pSpell, classmask))
					{
						return i;
					}
				}
			}
		}
	}

	return -1;
}

int GetSelfBuffBySPA(int spa, bool bIncrease, int startslot)
{
	CHARINFO2* pChar2 = GetCharInfo2();
	if (!pChar2)
		return -1;

	for (int i = startslot; i < NUM_LONG_BUFFS; i++)
	{
		if (SPELL * pSpell = GetSpellByID(pChar2->Buff[i].SpellID))
		{
			if (int base = ((EQ_Spell*)pSpell)->SpellAffectBase(spa))
			{
				switch (spa)
				{
				case 3: // Movement Rate
					if (!bIncrease && base < 0)
					{
						// below 0 means its a snare above its runspeed increase...
						return i;
					}
					else if (bIncrease && base > 0)
					{
						return i;
					}
					return -1;

				case 11: // Melee Speed
					if (!bIncrease && base < 100)
					{
						// below 100 means its a slow above its haste...
						return i;
					}
					else if (bIncrease && base > 100)
					{
						return i;
					}
					return -1;

				case 59: // Damage Shield
					if (!bIncrease && base > 0)
					{
						// decreased DS
						return i;
					}
					else if (bIncrease && base < 0)
					{
						// increased DS
						return i;
					}
					return -1;

				case 121: // Reverse Damage Shield
					if (!bIncrease && base > 0)
					{
						// decreased DS
						return i;
					}
					else if (bIncrease && base < 0)
					{
						// increased DS
						return i;
					}
					return -1;

				default:
					return i;
				}
			}
		}
	}

	return -1;
}

int GetSelfShortBuffBySPA(int spa, bool bIncrease, int startslot)
{
	CHARINFO2* pChar2 = GetCharInfo2();
	if (!pChar2)
		return -1;

	for (int i = startslot; i < NUM_SHORT_BUFFS; i++)
	{
		if (SPELL * pSpell = GetSpellByID(pChar2->ShortBuff[i].SpellID))
		{
			if (int base = ((EQ_Spell*)pSpell)->SpellAffectBase(spa))
			{
				switch (spa)
				{
				case 3: // Movement Rate
					if (!bIncrease && base < 0)
					{
						// below 0 means its a snare above its runspeed increase...
						return i;
					}
					else if (bIncrease && base > 0)
					{
						return i;
					}
					return -1;

				case 11: // Melee Speed
					if (!bIncrease && base < 100)
					{
						// below 100 means its a slow above its haste...
						return i;
					}
					else if (bIncrease && base > 100)
					{
						return i;
					}
					return -1;

				case 59: // Damage Shield
					if (!bIncrease && base > 0)
					{
						// decreased DS
						return i;
					}
					else if (bIncrease && base < 0)
					{
						// increased DS
						return i;
					}
					return -1;

				case 121: // Reverse Damage Shield
					if (!bIncrease && base > 0)
					{
						// decreased DS
						return i;
					}
					else if (bIncrease && base < 0)
					{
						// increased DS
						return i;
					}
					return -1;

				default:
					return i;
				}
			}
		}
	}

	return -1;
}

int GetSpellCategory(SPELL * pSpell)
{
	if (pSpell)
	{
		if (pSpell->CannotBeScribed)
		{
			if (SPELL * pTrigger = GetSpellParent(pSpell->ID))
			{
				return pTrigger->Category;
			}
		}
		else
		{
			return pSpell->Category;
		}
	}

	return 0;
}

int GetSpellSubcategory(SPELL * pSpell)
{
	if (pSpell)
	{
		if (pSpell->CannotBeScribed)
		{
			if (SPELL * pTrigger = GetSpellParent(pSpell->ID))
			{
				return pTrigger->Subcategory;
			}
		}
		else
		{
			return pSpell->Subcategory;
		}
	}

	return 0;
}

bool IsAegoSpell(SPELL * pSpell)
{
	if (pSpell->CannotBeScribed)
	{
		if (SPELL * pTrigger = GetSpellParent(pSpell->ID))
		{
			if ((pTrigger->Subcategory == 1) || (pTrigger->Subcategory == 112))
			{
				int base = ((EQ_Spell*)pSpell)->SpellAffectBase(1); // check if it has ac?
				if (base)
				{
					return true;
				}
			}
		}
	}
	else
	{
		if ((pSpell->Subcategory == 1) || (pSpell->Subcategory == 112))
		{
			int base = ((EQ_Spell*)pSpell)->SpellAffectBase(1);
			if (base)
			{
				return true;
			}
		}
	}

	return false;
}

bool IsSpellUsableForClass(SPELL * pSpell, unsigned int classmask)
{
	if (classmask != Unknown)
	{
		for (int N = 0; N < 16; N++)
		{
			if (classmask & (1 << N))
			{
				if (pSpell->ClassLevel[N] != 255)
					return true;
			}
		}
		return false;
	}
	return true;
}

int GetSpellRankByName(const char* SpellName)
{
	// uppercase the string
	char szTemp[256];
	strcpy_s(szTemp, SpellName);
	_strupr_s(szTemp);

	if (endsWith(szTemp, " II"))
		return 2;
	if (endsWith(szTemp, " III"))
		return 3;
	if (endsWith(szTemp, " IV"))
		return 4;
	if (endsWith(szTemp, " V"))
		return 5;
	if (endsWith(szTemp, " VI"))
		return 6;
	if (endsWith(szTemp, " VII"))
		return 7;
	if (endsWith(szTemp, " VIII"))
		return 8;
	if (endsWith(szTemp, " IX"))
		return 9;
	if (endsWith(szTemp, " X"))
		return 10;
	if (endsWith(szTemp, " XI"))
		return 11;
	if (endsWith(szTemp, " XII"))
		return 12;
	if (endsWith(szTemp, " XIII"))
		return 13;
	if (endsWith(szTemp, " XIV"))
		return 14;
	if (endsWith(szTemp, " XV"))
		return 15;
	if (endsWith(szTemp, " XVI"))
		return 16;
	if (endsWith(szTemp, " XVII"))
		return 17;
	if (endsWith(szTemp, " XVIII"))
		return 18;
	if (endsWith(szTemp, " XIX"))
		return 19;
	if (endsWith(szTemp, " XX"))
		return 20;
	if (endsWith(szTemp, " XXI"))
		return 21;
	if (endsWith(szTemp, " XXII"))
		return 22;
	if (endsWith(szTemp, " XXIII"))
		return 23;
	if (endsWith(szTemp, " XXIV"))
		return 24;
	if (endsWith(szTemp, " XXV"))
		return 25;
	if (endsWith(szTemp, " XXVI"))
		return 26;
	if (endsWith(szTemp, " XXVII"))
		return 27;
	if (endsWith(szTemp, " XXVIII"))
		return 28;
	if (endsWith(szTemp, " XXIX"))
		return 29;
	if (endsWith(szTemp, " XXX"))
		return 30;

	if (endsWith(szTemp, ".II"))
		return 2;
	if (endsWith(szTemp, ".III"))
		return 3;

	return 0;
}

void TruncateSpellRankName(char* SpellName)
{
	if (char* pch = strrchr(SpellName, '.'))
	{
		pch -= 3;
		*pch = 0;
	}
}

void RemoveBuff(SPAWNINFO * pChar, char* szLine)
{
	bool bPet = false;
	bool bAll = false;
	char szCmd[MAX_STRING] = { 0 };
	GetArg(szCmd, szLine, 1);

	if (!_stricmp(szCmd, "-pet"))
	{
		bPet = true;
		GetArg(szCmd, szLine, 2);
	}
	else if (!_stricmp(szCmd, "-both"))
	{
		bAll = true;
		GetArg(szCmd, szLine, 2);
	}

	if (szCmd && szCmd[0] != '\0')
	{
		if (bPet || bAll)
		{
			if (pPetInfoWnd && szLine && szLine[0] != 0)
			{
				for (int nBuff = 0; nBuff < NUM_BUFF_SLOTS; nBuff++)
				{
					if (SPELL * pBuffSpell = GetSpellByID(pPetInfoWnd->Buff[nBuff]))
					{
						if (!_strnicmp(pBuffSpell->Name, szCmd, strlen(szCmd)))
						{
							((PcZoneClient*)pPCData)->RemovePetEffect(nBuff);
							break;
						}
					}
				}
			}
			if (bPet) return;
		}

		if (CHARINFO2 * pChar2 = GetCharInfo2())
		{
			for (int nBuff = 0; nBuff < NUM_LONG_BUFFS; nBuff++)
			{
				if (pChar2->Buff[nBuff].SpellID == 0 || pChar2->Buff[nBuff].SpellID == -1)
					continue;

				if (SPELL * pBuffSpell = GetSpellByID(pChar2->Buff[nBuff].SpellID))
				{
					if (!_strnicmp(pBuffSpell->Name, szCmd, strlen(szCmd)))
					{
						((PcZoneClient*)pPCData)->RemoveBuffEffect(nBuff, ((SPAWNINFO*)pLocalPlayer)->SpawnID);
						return;
					}
				}
			}

			for (int nBuff = 0; nBuff < NUM_SHORT_BUFFS; nBuff++)
			{
				if (pChar2->ShortBuff[nBuff].SpellID == 0 || pChar2->ShortBuff[nBuff].SpellID == -1)
					continue;
				if (SPELL * pBuffSpell = GetSpellByID(pChar2->ShortBuff[nBuff].SpellID))
				{
					if (!_strnicmp(pBuffSpell->Name, szCmd, strlen(szCmd)))
					{
						((PcZoneClient*)pPCData)->RemoveBuffEffect(nBuff + NUM_LONG_BUFFS, ((SPAWNINFO*)pLocalPlayer)->SpawnID);
						//pPCData->RemoveMyAffect(nBuff + NUM_LONG_BUFFS);
						return;
					}
				}
			}
		}
	}
}

void RemovePetBuff(SPAWNINFO * pChar, char* szLine)
{
	if (pPetInfoWnd && szLine && szLine[0] != '\0')
	{
		for (int nBuff = 0; nBuff < NUM_BUFF_SLOTS; nBuff++)
		{
			if (SPELL * pBuffSpell = GetSpellByID(pPetInfoWnd->Buff[nBuff]))
			{
				if (!_strnicmp(pBuffSpell->Name, szLine, strlen(szLine)))
				{
					((PcZoneClient*)pPCData)->RemovePetEffect(nBuff);
					return;
				}
			}
		}
	}
}

bool StripQuotes(char* str)
{
	bool bRet = false;
	if (strchr(str, '"'))
		bRet = true;
	char* s, * d;
	for (s = d = str; *d = *s; d += (*s++ != '"'));
	return bRet;
}

DWORD __stdcall RefreshKeyRingThread(void* pData)
{
	RefreshKeyRingsThreadData* kr = (RefreshKeyRingsThreadData*)pData;
	if (!kr) return 0;

	CSidlScreenWnd* krwnd = kr->phWnd;
	bool bExact = kr->bExact;
	bool bUseCmd = kr->bUseCmd;
	bool bToggled = false;
	char szItemName[256] = { 0 };
	strcpy_s(szItemName, kr->ItemName);
	delete kr;

	if (!krwnd) return 0;

	CTabWnd* pTab = (CTabWnd*)krwnd->GetChildItem(KeyRingTab);
	if (!pTab) return 0;

	if (!krwnd->IsVisible())
	{
		bToggled = true;
		krwnd->Activate();
		krwnd->StoreIniVis();
	}

	bool bRefresh = false;
	CListWnd* clist = nullptr;

	if (GetMountCount() > 0)
	{
		// tab 0 is the mount key ring page...
		pTab->SetPage(0, true);

		if (clist = (CListWnd*)krwnd->GetChildItem(MountWindowList))
		{
			ULONGLONG now = MQGetTickCount64();
			while (!clist->ItemsArray.Count)
			{
				Sleep(10);
				if (now + 5000 < MQGetTickCount64())
				{
					WriteChatColor("Timed out waiting for mount keyring refresh", CONCOLOR_YELLOW);
					break;
				}
			}
		}
	}

	if (GetIllusionCount() > 0)
	{
		// tab 1 is the illusion key ring page...
		pTab->SetPage(1, true);

		if (clist = (CListWnd*)krwnd->GetChildItem(IllusionWindowList))
		{
			ULONGLONG now = MQGetTickCount64();
			while (!clist->ItemsArray.Count)
			{
				Sleep(10);
				if (now + 5000 < MQGetTickCount64())
				{
					WriteChatColor("Timed out waiting for illusion keyring refresh", CONCOLOR_YELLOW);
					break;
				}
			}
		}
	}

	if (GetFamiliarCount() > 0)
	{
		//tab 2 is the familiar key ring page...
		pTab->SetPage(2, true);

		if (clist = (CListWnd*)krwnd->GetChildItem(FamiliarWindowList))
		{
			ULONGLONG now = MQGetTickCount64();
			while (!clist->ItemsArray.Count)
			{
				Sleep(10);
				if (now + 5000 < MQGetTickCount64())
				{
					WriteChatColor("Timed out waiting for familiar keyring refresh", CONCOLOR_YELLOW);
					break;
				}
			}
		}
	}

	WeDidStuff();

	if (bToggled)
	{
		krwnd->Deactivate();
		krwnd->StoreIniVis();
	}

	if (bUseCmd && clist && clist->ItemsArray.Count)
	{
		UseItemCmd(GetCharInfo()->pSpawn, szItemName);
	}

	return 0;
}

void RefreshKeyRings(void* kr)
{
	DWORD nThread = 0;
	CreateThread(nullptr, 0, RefreshKeyRingThread, kr, NULL, &nThread);
}

int GetMountCount()
{
	int Count = 0;
	if (CHARINFO * pChar = GetCharInfo())
	{
#ifdef NEWCHARINFO
		return pChar->MountKeyRingItems.Items.Size;
#else
		if (pChar && pChar->pMountsArray && pChar->pMountsArray->Mounts)
		{
			for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++)
			{
				if (CONTENTS * pItem = pChar->pMountsArray->Mounts[nSlot])
				{
					Count++;
				}
			}
		}
#endif
	}
	return Count;
}

int GetIllusionCount()
{
	int Count = 0;
	if (CHARINFO * pChar = GetCharInfo())
	{
#ifdef NEWCHARINFO
		return pChar->IllusionKeyRingItems.Items.Size;
#else
		if (pChar && pChar->pIllusionsArray && pChar->pIllusionsArray->Illusions)
		{
			for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++)
			{
				if (CONTENTS * pItem = pChar->pIllusionsArray->Illusions[nSlot])
				{
					Count++;
				}
			}
		}
#endif
	}
	return Count;
}

int GetFamiliarCount()
{
	int Count = 0;
	if (CHARINFO * pChar = GetCharInfo())
	{
#ifdef NEWCHARINFO
		return pChar->FamiliarKeyRingItems.Items.Size;
#else
		if (pChar && pChar->pFamiliarArray && pChar->pFamiliarArray->Familiars)
		{
			for (unsigned long nSlot = 0; nSlot < MAX_KEYRINGITEMS; nSlot++)
			{
				if (CONTENTS * pItem = pChar->pFamiliarArray->Familiars[nSlot])

				{
					Count++;
				}
			}
		}
#endif
	}
	return Count;
}

int GetKeyRingIndex(KeyRingType KeyRing, const char* szItemName, bool bExact, bool usecmd)
{
	int index = 0;

	if (CSidlScreenWnd * krwnd = (CSidlScreenWnd*)FindMQ2Window(KeyRingWindowParent))
	{
		CListWnd* clist = nullptr;

		if (KeyRing == eFamiliar)
			clist = (CListWnd*)krwnd->GetChildItem(FamiliarWindowList);
		else if (KeyRing == eIllusion)
			clist = (CListWnd*)krwnd->GetChildItem(IllusionWindowList);
		else if (KeyRing == eMount)
			clist = (CListWnd*)krwnd->GetChildItem(MountWindowList);

		if (clist)
		{
			if (int numitems = clist->ItemsArray.GetCount())
			{
				for (int i = 0; i < numitems; i++)
				{
					CXStr Str = clist->GetItemText(i, 2);

					if (!Str.empty())
					{
						if (bExact)
						{
							if (!_stricmp(szItemName, Str.c_str()))
							{
								index = i + 1;
								break;
							}
						}
						else
						{
							if (ci_find_substr(Str, szItemName) != -1)
							{
								index = i + 1;
								break;
							}
						}
					}
				}
			}
			else
			{
				if (CONTENTS * pCont = FindItemByName(szItemName, bExact))
				{
					bool bKeyring = false;
					if (CHARINFO * pCharInfo = GetCharInfo())
					{
						bKeyring = pCont->GetGlobalIndex().IsKeyRingLocation();
					}

					if (bKeyring)
					{
						// if the keyring lists has 0 items in it, we arrive here...
						// its not filled in until you open the mount keyring tab in the inventory window...
						// since numitems was 0, we know the user hasnt opened up his inventory
						// and been on the mount key ring tab...so we start a thread and force that... -eqmule

						RefreshKeyRingsThreadData* kr = new RefreshKeyRingsThreadData;
						kr->bExact = bExact;
						kr->phWnd = krwnd;
						kr->bUseCmd = usecmd;
						strcpy_s(kr->ItemName, szItemName);
						RefreshKeyRings(kr);
					}
				}
			}
		}
	}

	return index;
}

void InitKeyRings()
{
	if (CSidlScreenWnd * krwnd = (CSidlScreenWnd*)FindMQ2Window(KeyRingWindowParent))
	{
		CListWnd* clist = nullptr;
		bool bRefresh = false;

		if (GetMountCount() > 0)
		{
			if (clist = (CListWnd*)krwnd->GetChildItem(MountWindowList))
			{
				if (!clist->ItemsArray.Count)
				{
					bRefresh = true;
				}
			}
		}

		if (GetIllusionCount() > 0)
		{
			if (clist = (CListWnd*)krwnd->GetChildItem(IllusionWindowList))
			{
				if (!clist->ItemsArray.Count)
				{
					bRefresh = true;
				}
			}
		}

		if (GetFamiliarCount() > 0)
		{
			if (clist = (CListWnd*)krwnd->GetChildItem(FamiliarWindowList))
			{
				if (!clist->ItemsArray.Count)
				{
					bRefresh = true;
				}
			}
		}

		// ok it seems like the player has mounts/illusions/familiars in his keyring
		// lets make sure we initialize it for the Mount/Illusion/Familiar TLO
		if (bRefresh)
		{
			//WriteChatColor("Mount/Illusion/Familiar key ring initialized", CONCOLOR_YELLOW);
			RefreshKeyRingsThreadData* kr = new RefreshKeyRingsThreadData;
			kr->phWnd = krwnd;
			kr->bExact = false;
			kr->bUseCmd = false;
			kr->ItemName[0] = 0;
			RefreshKeyRings(kr);
		}
	}
}

//.text:00638049                 mov     ecx, pinstPCData_x
//.text:0063804F                 push    0
//.text:00638051                 push    0
//.text:00638053                 add     ecx, 1FE0h
//.text:00638059                 call    ?MakeMeVisible@CharacterZoneClient@@QAEXH_N@Z ; CharacterZoneClient::MakeMeVisible(int,bool)
void MakeMeVisible(SPAWNINFO * pChar, char* szLine)
{
	if (pCharData) {
		pCharData->MakeMeVisible(0, false);
	}
}

// ***************************************************************************
// Function:    RemoveAura
// Description: Removes auras
// Usage:       /removeaura <name> or <partial name>
// Author:      EqMule
// ***************************************************************************
void RemoveAura(SPAWNINFO * pChar, char* szLine)
{
	if (!pAuraWnd)
		return;

	if (!szLine || (szLine[0] == 0))
	{
		WriteChatColor("Usage: /removeaura <auraname> or <aurapartialname>", CONCOLOR_LIGHTBLUE);
		return;
	}

	char szOut[MAX_STRING] = { 0 };
	char szCmp[MAX_STRING] = { 0 };
	strcpy_s(szCmp, szLine);

	if (CListWnd * clist = (CListWnd*)pAuraWnd->GetChildItem("AuraList"))
	{
		for (int i = 0; i < clist->ItemsArray.Count; i++)
		{
			CXStr Str = clist->GetItemText(i, 1);

			if (!Str.empty())
			{
				strcpy_s(szOut, Str.c_str());
				_strlwr_s(szOut);
				_strlwr_s(szCmp);

				if (strstr(szOut, szCmp))
				{
					clist->SetCurSel(i);
					pAuraWnd->WndNotification(clist, XWM_MENUSELECT, (void*)1);
				}
			}
		}
	}
}

bool GetAllMercDesc(std::map<int, MercDesc> & minfo)
{
	if (!pMercInfo)
		return false;

	if (MERCSLIST * pmlist = pMercInfo->pMercsList)
	{
		for (int i = 0; i < pMercInfo->MercenaryCount; i++)
		{
			MercDesc& outDesc = minfo[i];

			int mdesc = pmlist->mercinfo[i].nMercDesc;
			std::string smdesc = pCDBStr->GetString(mdesc, eMercenarySubCategoryDescription);
			size_t pos = 0;

			if ((pos = smdesc.find("Race: ")) != std::string::npos)
			{
				outDesc.Race = smdesc.substr(pos + 6);
				if ((pos = outDesc.Race.find("<br>")) != std::string::npos)
				{
					outDesc.Race.erase(pos);
				}
			}

			if ((pos = smdesc.find("Type: ")) != std::string::npos)
			{
				outDesc.Type = smdesc.substr(pos + 6);
				if ((pos = outDesc.Type.find("<br>")) != std::string::npos)
				{
					outDesc.Type.erase(pos);
				}
			}

			if ((pos = smdesc.find("Confidence: ")) != std::string::npos)
			{
				outDesc.Confidence = smdesc.substr(pos + 12);
				if ((pos = outDesc.Confidence.find("<br>")) != std::string::npos)
				{
					outDesc.Confidence.erase(pos);
				}
			}

			if ((pos = smdesc.find("Proficiency: ")) != std::string::npos)
			{
				outDesc.Proficiency = smdesc.substr(pos + 13);
				if ((pos = outDesc.Proficiency.find("<br>")) != std::string::npos)
				{
					outDesc.Proficiency.erase(pos);
				}
			}
		}
	}

	return true;
}

bool IsActiveAA(const char* pSpellName)
{
	int level = -1;
	if (PSPAWNINFO pMe = (PSPAWNINFO)pLocalPlayer)
	{
		level = pMe->Level;
	}

	for (int nAbility = 0; nAbility < AA_CHAR_MAX_REAL; nAbility++)
	{
		if (ALTABILITY * pAbility = GetAAByIdWrapper(pPCData->GetAlternateAbilityId(nAbility), level))
		{
			if (!_stricmp(pSpellName, pCDBStr->GetString(pAbility->nName, eAltAbilityName)))
			{
				if (pAbility->SpellID <= 0)
				{
					return true;
				}
			}
		}
	}

	return false;
}

struct Personal_Loot
{
	CButtonWnd* NPC_Name = nullptr;
	CButtonWnd* Item = nullptr;
	CButtonWnd* Loot = nullptr;
	CButtonWnd* Leave = nullptr;
	CButtonWnd* AN = nullptr;
	CButtonWnd* AG = nullptr;
	CButtonWnd* Never = nullptr;
};

CXWnd* GetAdvLootPersonalListItem(DWORD ListIndex, DWORD type)
{
	if (CListWnd * clist = (CListWnd*)pAdvancedLootWnd->GetChildItem("ADLW_PLLList"))
	{
		Personal_Loot pPAdvLoot;
		bool bFound = false;
		int listindex = -1;

		CXWnd* pFirstWnd = clist->GetFirstChildWnd();
		CXWnd* pNextWnd = pFirstWnd;

		for (int i = 0; i < clist->ItemsArray.Count; i++)
		{
			if (pNextWnd)
			{
				pPAdvLoot.NPC_Name = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pPAdvLoot.Item = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pPAdvLoot.Loot = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pPAdvLoot.Leave = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pPAdvLoot.Never = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pPAdvLoot.AN = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pPAdvLoot.AG = (CButtonWnd*)pNextWnd->GetFirstChildWnd();

				if (pNextWnd && pNextWnd->GetNextSiblingWnd())
				{
					pNextWnd = pNextWnd->GetNextSiblingWnd();
				}
			}

			if (ListIndex == i)
			{
				bFound = true;
				break;
			}
		}

		if (bFound)
		{
			CXWnd* ptr = nullptr;

			switch (type)
			{
			case 0:
				ptr = (CXWnd*)pPAdvLoot.NPC_Name;
				break;
			case 1:
				ptr = (CXWnd*)pPAdvLoot.Item;
				break;
			case 2:
				ptr = (CXWnd*)pPAdvLoot.Loot;
				break;
			case 3:
				ptr = (CXWnd*)pPAdvLoot.Leave;
				break;
			case 4:
				ptr = (CXWnd*)pPAdvLoot.Never;
				break;
			case 5:
				ptr = (CXWnd*)pPAdvLoot.AN;
				break;
			case 6:
				ptr = (CXWnd*)pPAdvLoot.AG;
				break;
			}

			return ptr;
		}
	}

	return nullptr;
}

struct Shared_Loot
{
	CButtonWnd* NPC_Name = nullptr;
	CButtonWnd* Item = nullptr;
	CButtonWnd* Status = nullptr;
	CButtonWnd* Action = nullptr;
	CButtonWnd* Manage = nullptr;
	CButtonWnd* AutoRoll = nullptr;
	CButtonWnd* ND = nullptr;
	CButtonWnd* GD = nullptr;
	CButtonWnd* NO = nullptr;
	CButtonWnd* AN = nullptr;
	CButtonWnd* AG = nullptr;
	CButtonWnd* NV = nullptr;
};

CXWnd* GetAdvLootSharedListItem(DWORD ListIndex, DWORD type)
{
	if (CListWnd * clist = (CListWnd*)pAdvancedLootWnd->GetChildItem("ADLW_CLLList"))
	{
		Shared_Loot pSAdvLoot;
		bool bFound = false;

		CXWnd* pFirstWnd = clist->GetFirstChildWnd();
		CXWnd* pNextWnd = pFirstWnd;

		for (int i = 0; i < clist->ItemsArray.Count; i++)
		{
			if (pNextWnd)
			{
				pSAdvLoot.NPC_Name = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.Item = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.Status = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.Action = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.Manage = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.AN = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.AG = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.AutoRoll = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.NV = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.ND = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.GD = (CButtonWnd*)pNextWnd->GetFirstChildWnd();
				pNextWnd = pNextWnd->GetNextSiblingWnd();
				pSAdvLoot.NO = (CButtonWnd*)pNextWnd->GetFirstChildWnd();

				if (pNextWnd && pNextWnd->GetNextSiblingWnd())
				{
					pNextWnd = pNextWnd->GetNextSiblingWnd();
				}
			}
			if (ListIndex == i)
			{
				bFound = true;
				break;
			}
		}

		// NPC_Name,Item,Status,Action,Manage,AN,AG,AutoRoll,NV,ND,GD,NO
		if (bFound)
		{
			CXWnd* ptr = nullptr;
			switch (type)
			{
			case 0:
				ptr = (CXWnd*)pSAdvLoot.NPC_Name;
				break;
			case 1:
				ptr = (CXWnd*)pSAdvLoot.Item;
				break;
			case 2:
				ptr = (CXWnd*)pSAdvLoot.Status;
				break;
			case 3:
				ptr = (CXWnd*)pSAdvLoot.Action;
				break;
			case 4:
				ptr = (CXWnd*)pSAdvLoot.Manage;
				break;
			case 5:
				ptr = (CXWnd*)pSAdvLoot.AN;
				break;
			case 6:
				ptr = (CXWnd*)pSAdvLoot.AG;
				break;
			case 7:
				ptr = (CXWnd*)pSAdvLoot.AutoRoll;
				break;
			case 8:
				ptr = (CXWnd*)pSAdvLoot.NV;
				break;
			case 9:
				ptr = (CXWnd*)pSAdvLoot.ND;
				break;
			case 10:
				ptr = (CXWnd*)pSAdvLoot.GD;
				break;
			case 11:
				ptr = (CXWnd*)pSAdvLoot.NO;
				break;
			case 12://root
				ptr = (CXWnd*)pSAdvLoot.Item;
				break;
			}

			return ptr;
		}
	}

	return nullptr;
}

bool LootInProgress(CAdvancedLootWnd * pAdvLoot, CListWnd * pPersonalList, CListWnd * pSharedList)
{
	if (pPersonalList)
	{
		for (int i = 0; i < pPersonalList->ItemsArray.Count; i++)
		{
			int listindex = (int)pPersonalList->GetItemData(i);
			if (listindex != -1)
			{
				AdvancedLootItem& lootItem = pAdvLoot->pPLootList->Items[listindex];
				if (lootItem.PLootInProgress || lootItem.CLootInProgress)
				{
					return true;
				}
			}
		}
	}

	if (pSharedList)
	{
		for (int i = 0; i < pSharedList->ItemsArray.Count; i++)
		{
			int listindex = (int)pSharedList->GetItemData(i);
			if (listindex != -1)
			{
				AdvancedLootItem& lootItem = pAdvLoot->pCLootList->Items[listindex];
				if (lootItem.PLootInProgress || lootItem.CLootInProgress)
				{
					return true;
				}
			}
		}
	}

	return false;
}

void WeDidStuff()
{
	gbCommandEvent = 1;
	gMouseEventTime = GetFastTime();
}

int GetFreeInventory(int nSize)
{
	int freeslots = 0;
	if (CHARINFO2 * pChar2 = GetCharInfo2()) {
		if (nSize) {
			for (DWORD slot = BAG_SLOT_START; slot < NUM_INV_SLOTS; slot++) {
				if (pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray && pChar2->pInventoryArray->InventoryArray[slot]) {
					if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[slot]) {
						if (GetItemFromContents(pItem)->Type == ITEMTYPE_PACK && GetItemFromContents(pItem)->SizeCapacity >= nSize) {
							if (!pItem->Contents.ContainedItems.pItems) {
								freeslots += GetItemFromContents(pItem)->Slots;
							}
							else {
								for (DWORD pslot = 0; pslot < (GetItemFromContents(pItem)->Slots); pslot++) {
									if (!pItem->GetContent(pslot)) {
										freeslots++;
									}
								}
							}
						}
					}
					else {
						freeslots++;
					}
				}
				else {
					freeslots++;
				}
			}
		}
		else {
			for (DWORD slot = BAG_SLOT_START; slot < NUM_INV_SLOTS; slot++) {
				if (!HasExpansion(EXPANSION_HoT) && slot > BAG_SLOT_START + 7) {
					break;
				}
				if (pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray && pChar2->pInventoryArray->InventoryArray[slot]) {
					if (CONTENTS * pItem = pChar2->pInventoryArray->InventoryArray[slot]) {
						if (GetItemFromContents(pItem)->Type == ITEMTYPE_PACK) {
							if (!pItem->Contents.ContainedItems.pItems) {
								freeslots += GetItemFromContents(pItem)->Slots;
							}
							else {
								for (DWORD pslot = 0; pslot < (GetItemFromContents(pItem)->Slots); pslot++) {
									if (!pItem->GetContent(pslot)) {
										freeslots++;
									}
								}
							}
						}
					}
					else {
						freeslots++;
					}
				}
				else {
					freeslots++;
				}
			}
		}
	}
	return freeslots;
}
bool CanItemMergeInPack(CONTENTS * pPack, CONTENTS * pItem)
{
	for (UINT i = 0; i < pPack->Contents.ContainedItems.Size; i++)
	{
		if (CONTENTS * pSlot = pPack->Contents.ContainedItems.pItems->Item[i])
		{
			if (pSlot->ID == pItem->ID)
			{
				if (PITEMINFO pItem2 = GetItemFromContents(pSlot))
				{
					if (pSlot->StackCount + pItem->StackCount <= (int)pItem2->StackSize)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}
void DoCommandf(char* szFormat, ...)
{
	va_list vaList;
	va_start(vaList, szFormat);
	int len = _vscprintf(szFormat, vaList) + 1;// _vscprintf doesn't count // terminating '\0'  
	if (char* szOutput = (char*)LocalAlloc(LPTR, len + 32)) {
		vsprintf_s(szOutput, len, szFormat, vaList);
		HideDoCommand((PSPAWNINFO)pLocalPlayer, szOutput, false);
		LocalFree(szOutput);
	}
}
bool CanItemGoInPack(CONTENTS * pPack, CONTENTS * pItem)
{
	//so cangoinbag doesnt actually check if there is any room, all it checks is IF there where room, could the item go in it.
	bool bRet = ((EQ_Item*)pItem)->CanGoInBag(&pPack);
	if (!bRet)
		return false;//no point in checking slots.
	for (UINT i = 0; i < pPack->Contents.ContainedItems.Size; i++)
	{
		if (CONTENTS * pSlot = pPack->Contents.ContainedItems.pItems->Item[i])
		{

		}
		else {
			return true;//free slot...
		}
	}
	return false;
}
bool WillFitInBank(CONTENTS * pContent)
{
	if (PITEMINFO pMyItem = GetItemFromContents(pContent))
	{
#ifdef NEWCHARINFO
		if (PCHARINFO pChar = GetCharInfo()) {
#else
		if (CHARINFONEW * pChar = (CHARINFONEW*)GetCharInfo()) {
#endif
			for (DWORD slot = 0; slot < pChar->BankItems.Items.Size; slot++) {
				if (CONTENTS * pCont = pChar->BankItems.Items[slot].pObject) {
					if (PITEMINFO pItem = GetItemFromContents(pCont))
					{
						if (pItem->Type == ITEMTYPE_PACK)
						{
							if (CanItemMergeInPack(pCont, pContent))
							{
								return true;
							}
							else if (CanItemGoInPack(pCont, pContent))
							{
								return true;
							}
						}
						else {//its not a pack but its an item, do we match?
							if (pCont->ID == pContent->ID)
							{
								if (pCont->StackCount + pContent->StackCount <= (int)pItem->StackSize)
								{
									return true;
								}
							}
						}
					}
				}
				else {//its empty of course we will fit.
					return true;
				}
			}
		}
	}
	return false;
}
bool WillFitInInventory(CONTENTS * pContent)
{
	if (PITEMINFO pMyItem = GetItemFromContents(pContent))
	{
		if (CHARINFO2 * pChar2 = (CHARINFO2*)GetCharInfo2()) {
			if (pChar2->pInventoryArray && pChar2->pInventoryArray->InventoryArray)
			{
				for (DWORD slot = BAG_SLOT_START; slot < NUM_INV_SLOTS; slot++)
				{
					if (CONTENTS * pCont = pChar2->pInventoryArray->InventoryArray[slot])
					{
						if (PITEMINFO pItem = GetItemFromContents(pCont))
						{
							if (pItem->Type == ITEMTYPE_PACK)
							{
								if (CanItemMergeInPack(pCont, pContent))
								{
									return true;
								}
								else if (CanItemGoInPack(pCont, pContent))
								{
									return true;
								}
							}
							else {//its not a pack but its an item, do we match?
								if (pCont->ID == pContent->ID)
								{
									if (pCont->StackCount + pContent->StackCount <= (int)pItem->StackSize)
									{
										return true;
									}
								}
							}
						}
					}
					else {//its empty of course we will fit.
						return true;
					}
				}
			}
		}
	}
	return false;
}
int GetGroupMemberClassByIndex(int N)
{
	if (PCHARINFO pChar = GetCharInfo()) {
		if (!pChar->pGroupInfo)
			return 0;
		if (pChar->pGroupInfo->pMember[N] && pChar->pGroupInfo->pMember[N]->pSpawn)
		{
			return pChar->pGroupInfo->pMember[N]->pSpawn->mActorClient.Class;
		}
	}
	return 0;
}

int GetRaidMemberClassByIndex(int N)
{
	if (pRaid && pRaid->Invited == 4) {
		if (pRaid->RaidMemberUsed[N])
			return pRaid->RaidMember[N].nClass;
	}
	return 0;
}

bool Anonymize(char* name, int maxlen, int NameFlag)
{
	if (GetGameState() != GAMESTATE_INGAME || !pLocalPlayer)
		return false;
	bool bisTarget = false;
	int isRmember = -1;
	bool isGmember = false;
	bool bChange = false;
	int ItsMe = _stricmp(((PSPAWNINFO)pLocalPlayer)->Name, name);
	if (ItsMe != 0)//well if it is me, then there is no point in checking if its a group member
		isGmember = IsGroupMember(name);
	if (!isGmember && ItsMe != 0)//well if it is me or a groupmember, then there is no point in checking if its a raid member
		isRmember = IsRaidMember(name);
	if (ItsMe != 0 && !isGmember && isRmember) {
		//my target?
		if (pTarget && ((PSPAWNINFO)pTarget)->Type != SPAWN_NPC)
		{
			if (!_strnicmp(((PSPAWNINFO)pTarget)->DisplayedName, name, strlen(((PSPAWNINFO)pTarget)->DisplayedName))) {
				bisTarget = true;
			}
		}
	}
	if (ItsMe == 0 || isGmember || isRmember != -1 || (bisTarget && pTarget)) {
		if (NameFlag == 1) {
			char buffer[L_tmpnam] = { 0 };
			tmpnam_s(buffer);
			char* pDest = strrchr(buffer, '\\');

			int len = strlen(name);
			for (int i = 1; i < len - 1; i++) {
				name[i] = '*';
			}
			strcat_s(name, 32, &pDest[1]);
			//strcat_s(name,32, GetClassShortName(theclass));
			return true;
		}
		if (gAnonymizeFlag == EAF_Class)
		{
			if (ItsMe == 0)
			{
				strncpy_s(name, 16, GetClassDesc(((PSPAWNINFO)pLocalPlayer)->mActorClient.Class), 15);
				if (NameFlag == 2)
				{
					strcat_s(name, 16, "_0");
				}
				bChange = true;
			}
			else if (bisTarget)
			{
				strncpy_s(name, 16, GetClassDesc(((PSPAWNINFO)pTarget)->mActorClient.Class), 15);
				bChange = true;
			}
			else if (isGmember)
			{
				int theclass = GetGroupMemberClassByIndex(isGmember);
				strncpy_s(name, 16, GetClassDesc(theclass), 15);
				if (NameFlag == 2)
				{
					char sztmp[16];
					sprintf_s(sztmp, "_%d", isGmember);
					strcat_s(name, 16, sztmp);
				}
				bChange = true;
			}
			else if (isRmember != -1)
			{
				int theclass = GetRaidMemberClassByIndex(isRmember);
				strncpy_s(name, 16, GetClassDesc(theclass), 15);
				if (NameFlag == 2)
				{
					char sztmp[16];
					sprintf_s(sztmp, "_%d", isRmember);
					strcat_s(name, 16, sztmp);
				}
				bChange = true;
			}
		}
		else {
			int len = strlen(name);
			bChange = true;
			for (int i = 1; i < len - 1; i++) {
				name[i] = '*';
			}
		}
	}
	return bChange;
}

// this is not ideal.
bool Anonymize2(CXStr & name, int LootFlag /*= 0*/)
{
	char szOut[MAX_STRING];
	strcpy_s(szOut, name.c_str());

	bool result = Anonymize(szOut, MAX_STRING, LootFlag);

	if (result)
	{
		name = szOut;
	}

	return result;
}

void UpdatedMasterLooterLabel()
{
	if (!pAdvancedLootWnd)
		return;

	CHARINFO* pChar = GetCharInfo();
	if (pChar || !pChar->pGroupInfo)
		return;

	CLabelWnd* MasterLooterLabel = (CLabelWnd*)pAdvancedLootWnd->GetChildItem("ADLW_CalculatedMasterLooter");
	if (!MasterLooterLabel)
		return;

	CXStr text;
	bool bFound = false;

	for (auto member : pChar->pGroupInfo->pMember)
	{
		if (member && member->MasterLooter)
		{
			text = member->Name;
			if (gAnonymize)
			{
				Anonymize2(text);
			}

			bFound = true;
			break;
		}
	}

	if (bFound)
	{
		MasterLooterLabel->SetWindowText(text);
	}
}

void* SPAWNINFO::GetCharacter() const
{
	return (void*)this->spawneqc_info;
}

int SPAWNINFO::GetZoneID() const
{
	return (int)this->Zone;
}

int SPAWNINFO::GetCurrentMana() const
{
	return (int)this->ManaCurrent;
}

int SPAWNINFO::GetMaxMana() const
{
	return (int)this->ManaMax;
}

int SPAWNINFO::GetCurrentEndurance() const
{
	return (int)this->EnduranceCurrent;
}

int SPAWNINFO::GetMaxEndurance() const
{
	return (int)this->EnduranceMax;
}

int SPAWNINFO::GetSpellCooldownETA() const
{
	return (int)this->SpellCooldownETA;
}

KeypressHandler* GetKeyPresshandler()
{
	KeypressHandler* kh = &KeypressHandler::Get();
	return kh;
}

AggroMeterManagerClient* GetAggroInfo()
{
	AggroMeterManagerClient* pAI = &AggroMeterManagerClient::Instance();
	return pAI;
}

ClientSOIManager* GetAuraMgr()
{
	ClientSOIManager* pSOI = ClientSOIManager::GetSingleton();
	return pSOI;
}

MercenaryAlternateAdvancementManagerClient* GetMercAltAbilities()
{
	// well this is work in progress noone uses this stuff right now except me -eqmule
	MercenaryAlternateAdvancementManagerClient* pMAAMC = &MercenaryAlternateAdvancementManagerClient::Instance();
	return pMAAMC;
}

CBroadcast* GetTextOverlay()
{
	CBroadcast* pBC = CBroadcast::Get();
	return pBC;
}

EQGroundItemListManager* GetItemList()
{
	EQGroundItemListManager* ptr = &EQGroundItemListManager::Instance();
	return ptr;
}

#if defined(LIVE)
#include <DbgHelp.h>
//PFINDFILEINPATHCALLBACK Pfindfileinpathcallback;

BOOL __stdcall Pfindfileinpathcallback(PCSTR filename, void* context)
{
	return true;
}
extern bool DirectoryExists(LPCTSTR lpszPath);
void CallMessage(DWORD pwnd)
{
	if (!DirectoryExists(gszLogPath))
	{
		CreateDirectory(gszLogPath, NULL);
	}
	char name[MAX_STRING] = { 0 };
	SYSTEMTIME t;
	GetSystemTime(&t);
	sprintf_s(name, "%s\\WindowBug_%4d%02d%02d_%02d%02d%02d.dmp", gszLogPath, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

	auto hFile = CreateFileA(name, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		char szTemp[MAX_STRING] = { 0 };
		sprintf_s(szTemp, "ERROR COULD NOT CREATE %s in CallMessage", name);
		MessageBox(NULL, szTemp, "Tell Eqmule", MB_SYSTEMMODAL | MB_OK);
		return;
	}

	BOOL dumped = MiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		hFile,
		MINIDUMP_TYPE(MiniDumpWithUnloadedModules | MiniDumpWithFullMemory | MiniDumpWithIndirectlyReferencedMemory),
		nullptr,
		nullptr,
		nullptr);
	CloseHandle(hFile);
	std::string Log = "You have stumbled upon a serious MQ2 Bug, please send this dump to eqmule@hotmail.com :\n\n";
	Log.append(name);
	MessageBox(NULL, Log.c_str(), "Send this dmp file to eqmule on discord/skype or mail.", MB_SYSTEMMODAL | MB_OK);
	//exit(0);
	return;

	DWORD64  dwAddress;
	DWORD  dwDisplacement;
	IMAGEHLP_LINE64 line;
	HANDLE hProcess = GetCurrentProcess();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_EXACT_SYMBOLS | SYMOPT_DEBUG | SYMOPT_IGNORE_CVREC | SYMOPT_OVERWRITE | SYMOPT_CASE_INSENSITIVE);

	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
	DWORD64  dwDisplacement2 = 0;
	DWORD64  dwAddress2 = 0;
	char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

	pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSymbol->MaxNameLen = MAX_SYM_NAME;
	char szpath[2048] = { 0 };

	GetCurrentDirectory(2048, szpath);
	MessageBox(NULL, szpath, "break in", MB_SYSTEMMODAL | MB_OK);
	DebugBreak();
	sprintf_s(szpath, "cache*C:\\Cache;srv*\\\\share\\MQ2Symbols");
	SymInitialize(hProcess, szpath, TRUE);

	BOOL bRet = SymGetSearchPath(hProcess, szpath, 2048);
	char* szOut = new char[MAX_STRING];
	char* szOutOrg = szOut;
	DWORD* Addresses = new DWORD[51];
	char* szTmp = new char[MAX_STRING];
	char* szTmpOrg = szTmp;
	std::string Str = "Look, you have stumbled across a serious bug in eq or mq2\nI will return 0 to not crash you here, but you could attach a debugger and:\nSet a breakpoint on \"return ret;\" in the \"int64_t EQUIStructs::GetClassMember(void* This, int ID)\" function in MQ2Utilities.cpp file then click ok and see what triggered this.\nSend eqmule@hotmail.com a screenshot of the callstack.\n\nIf you Would like to break into the debugger at this point click YES otherwise click NO to continue.\n\n";// [50];
	int numframes = CaptureStackBackTrace(0, 50, (void**)Addresses, NULL);
	HMODULE hMod = 0;
	for (int i = 0; i < numframes; i++)
	{
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)Addresses[i], &hMod);
		dwAddress = Addresses[i]; // Address you want to check on.
		dwAddress2 = dwAddress;
		if (SymGetLineFromAddr64(hProcess, dwAddress, &dwDisplacement, &line))
		{
			Sleep(0);//it worked...
		}
		GetModuleFileName(hMod, szOut, 2048);
		SymFromAddr(hProcess, dwAddress2, &dwDisplacement2, pSymbol);
		SYMSRV_INDEX_INFO sii;
		sii.sizeofstruct = sizeof(SYMSRV_INDEX_INFO);
		SymSrvGetFileIndexInfo(szOut, &sii, NULL);
		char szFound[2048] = { 0 };
		char pFound[2048] = { 0 };
		char* pDest = 0;
		if (pDest = strrchr(szOut, '\\'))
		{
			pDest[0] = '\0';
			pDest++;
		}
		sprintf_s(szTmp, MAX_STRING, "%s!%s Line %d", pDest, pSymbol->Name, line.LineNumber);
		Str.append(szTmp);
		Str.append("\n");
	}
	delete szTmpOrg;
	delete Addresses;
	delete szOutOrg;
	SymCleanup(hProcess);
	char szTitle[MAX_STRING] = { 0 };
	//char *szTemp2 = new char[Str.size() + 2048];
	sprintf_s(szTitle, "Bad Function call detected in GetClassMember, pWnd was: %x that is NOT a valid CXWnd* pointer for sure...", pwnd);

	int ret = MessageBox(NULL, Str.c_str(), szTitle, MB_YESNO | MB_SYSTEMMODAL);
	if (ret == IDYES)
	{
		DebugBreak();
	}
}

#endif // defined(LIVE)

// this function performs a better rand since it removes the random bias
// towards the low end if the range of rand() isn't divisible by max - min + 1
int RangeRandom(int min, int max)
{
	int n = max - min + 1;
	int remainder = RAND_MAX % n;
	int x;
	do {
		x = rand();
	} while (x >= RAND_MAX - remainder);
	return min + x % n;

}

//============================================================================

ITEMINFO* GetItemFromContents(CONTENTS * c)
{
	if (!c)
		return nullptr;

	return c->Item1 ? c->Item1 : c->Item2;
}

struct EnumWindowsData
{
	HWND outHWnd;
	DWORD processId;
};

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	EnumWindowsData* enumData = reinterpret_cast<EnumWindowsData*>(lParam);

	// Get the process id for the window.
	DWORD dwProcessId = 0;
	GetWindowThreadProcessId(hWnd, &dwProcessId);

	// Only check windows in the current process
	if (enumData->processId == dwProcessId)
	{
		char szClass[24] = { 0 };
		GetClassName(hWnd, szClass, 23);

		// If its the EverQuest window class, return it.
		if (strcmp(szClass, "_EverQuestwndclass") == 0)
		{
			enumData->outHWnd = hWnd;
			return FALSE;
		}
	}

	return TRUE;
}

EQLIB_API HWND GetEQWindowHandle()
{
	DWORD dwProcessId = GetCurrentProcessId();

	EnumWindowsData enumData;
	enumData.outHWnd = nullptr;
	enumData.processId = dwProcessId;

	EnumWindows(EnumWindowsProc, (LPARAM)& enumData);

	return enumData.outHWnd;
}
