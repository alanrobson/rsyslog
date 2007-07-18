/* parsing routines for the counted string class. for generic
 * informaton see parse.h.
 *
 * begun 2005-09-15 rgerhards
 *
 * Copyright 2005
 *     Rainer Gerhards and Adiscon GmbH. All Rights Reserved.
 *     This code is placed under the GPL.
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "rsyslog.h"
#include "net.h" /* struct NetAddr */
#include "parse.h"

/* ################################################################# *
 * private members                                                   *
 * ################################################################# */



/* ################################################################# *
 * public members                                                    *
 * ################################################################# */


/**
 * Destruct a rsPars object and its associated string.
 * rgerhards, 2005-09-26
 */
rsRetVal rsParsDestruct(rsParsObj *pThis)
{
	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	if(pThis->pCStr != NULL)
		RSFREEOBJ(pThis->pCStr);
	RSFREEOBJ(pThis);
	return RS_RET_OK;
}


/**
 * Construct a rsPars object.
 */
rsRetVal rsParsConstruct(rsParsObj **ppThis)
{
	rsParsObj *pThis;

	assert(ppThis != NULL);

	if((pThis = (rsParsObj*) calloc(1, sizeof(rsParsObj))) == NULL)
		return RS_RET_OUT_OF_MEMORY;

	rsSETOBJTYPE(pThis, OIDrsPars);

	*ppThis = pThis;
	return RS_RET_OK;
}

/**
 * Construct a rsPars object and populate it with a
 * classical zero-terinated C-String.
 * rgerhards, 2005-09-27
 */
rsRetVal rsParsConstructFromSz(rsParsObj **ppThis, unsigned char *psz)
{
	rsParsObj *pThis;
	rsCStrObj *pCS;
	rsRetVal iRet;

	assert(ppThis != NULL);
	assert(psz != NULL);

	/* create string for parser */
	if((iRet = rsCStrConstructFromszStr(&pCS, psz)) != RS_RET_OK)
		return(iRet);

	/* create parser */
	if((iRet = rsParsConstruct(&pThis)) != RS_RET_OK) {
		RSFREEOBJ(pCS);
		return(iRet);
	}

	/* assign string to parser */
	if((iRet = rsParsAssignString(pThis, pCS)) != RS_RET_OK) {
		rsParsDestruct(pThis);
		return(iRet);
	}

	*ppThis = pThis;
	return RS_RET_OK;
}

/**
 * Assign the to-be-parsed string.
 */
rsRetVal rsParsAssignString(rsParsObj *pThis, rsCStrObj *pCStr)
{
	rsCHECKVALIDOBJECT(pThis, OIDrsPars);
	rsCHECKVALIDOBJECT(pCStr, OIDrsCStr);
	
	pThis->pCStr = pCStr;
	pThis->iCurrPos = 0;

	return RS_RET_OK;
}

/* parse an integer. The parse pointer is advanced to the
 * position directly after the last digit. If no digit is
 * found at all, an error is returned and the parse pointer
 * is NOT advanced.
 * PORTABILITY WARNING: this function depends on the
 * continues representation of digits inside the character
 * set (as in ASCII).
 * rgerhards 2005-09-27
 */
rsRetVal parsInt(rsParsObj *pThis, int* pInt)
{
	unsigned char *pC;
	int iVal;

	rsCHECKVALIDOBJECT(pThis, OIDrsPars);
	assert(pInt != NULL);

	iVal = 0;
	pC = rsCStrGetBufBeg(pThis->pCStr) + pThis->iCurrPos;

	/* order of checks is important, else we might do
	 * mis-addressing! (off by one)
	 */
	if(pThis->iCurrPos >= rsCStrLen(pThis->pCStr))
		return RS_RET_NO_MORE_DATA;
	if(!isdigit((int)*pC))
		return RS_RET_NO_DIGIT;

	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr) && isdigit((int)*pC)) {
		iVal = iVal * 10 + *pC - '0';
		++pThis->iCurrPos;
		++pC;
	}

	*pInt = iVal;

	return RS_RET_OK;
}

/* Skip everything up to a specified character.
 * Returns with ParsePointer set BEHIND this character.
 * Returns RS_RET_OK if found, RS_RET_NOT_FOUND if not
 * found. In that case, the ParsePointer is moved to the
 * last character of the string.
 * 2005-09-19 rgerhards
 */
rsRetVal parsSkipAfterChar(rsParsObj *pThis, char c)
{
	register unsigned char *pC;
	rsRetVal iRet;

	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	pC = rsCStrGetBufBeg(pThis->pCStr);

	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr)) {
		if(pC[pThis->iCurrPos] == c)
			break;
		++pThis->iCurrPos;
	}

	/* delimiter found? */
	if(pC[pThis->iCurrPos] == c) {
		if(pThis->iCurrPos+1 < rsCStrLen(pThis->pCStr)) {
			iRet = RS_RET_OK;
			pThis->iCurrPos++; /* 'eat' delimiter */
		} else {
			iRet = RS_RET_FOUND_AT_STRING_END;
		}
	} else {
		iRet = RS_RET_NOT_FOUND;
	}

	return iRet;
}

/* Skip whitespace. Often used to trim parsable entries.
 * Returns with ParsePointer set to first non-whitespace
 * character (or at end of string).
 */
rsRetVal parsSkipWhitespace(rsParsObj *pThis)
{
	register unsigned char *pC;

	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	pC = rsCStrGetBufBeg(pThis->pCStr);

	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr)) {
		if(!isspace((int)*(pC+pThis->iCurrPos)))
			break;
		++pThis->iCurrPos;
	}

	return RS_RET_OK;
}

/* Parse string up to a delimiter.
 *
 * Input:
 * cDelim - the delimiter
 *   The following two are for whitespace stripping,
 *   0 means "no", 1 "yes"
 *   - bTrimLeading
 *   - bTrimTrailing
 * 
 * Output:
 * ppCStr Pointer to the parsed string - must be freed by caller!
 */
rsRetVal parsDelimCStr(rsParsObj *pThis, rsCStrObj **ppCStr, char cDelim, int bTrimLeading, int bTrimTrailing)
{
	register unsigned char *pC;
	rsCStrObj *pCStr;
	rsRetVal iRet;

	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	if((pCStr = rsCStrConstruct()) == NULL)
		return RS_RET_OUT_OF_MEMORY;

	if(bTrimLeading)
		parsSkipWhitespace(pThis);

	pC = rsCStrGetBufBeg(pThis->pCStr) + pThis->iCurrPos;

	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr)
	      && *pC != cDelim) {
		if((iRet = rsCStrAppendChar(pCStr, *pC)) != RS_RET_OK) {
			RSFREEOBJ(pCStr);
			return(iRet);
		}
		++pThis->iCurrPos;
		++pC;
	}
	
	if(*pC == cDelim) {
		++pThis->iCurrPos; /* eat delimiter */
	}

	/* We got the string, now take it and see if we need to
	 * remove anything at its end.
	 */
	if((iRet = rsCStrFinish(pCStr)) != RS_RET_OK) {
		RSFREEOBJ(pCStr);
		return(iRet);
	}

	if(bTrimTrailing) {
		if((iRet = rsCStrTrimTrailingWhiteSpace(pCStr)) 
		   != RS_RET_OK) {
			RSFREEOBJ(pCStr);
			return iRet;
		}
	}

	/* done! */
	*ppCStr = pCStr;
	return RS_RET_OK;
}

/* Parse a quoted string ("-some-data") from the given position.
 * Leading whitespace before the first quote is skipped. During
 * parsing, escape sequences are detected and converted:
 * \\ - backslash character
 * \" - quote character
 * any other value \<somechar> is reserved for future use.
 *
 * After return, the parse pointer is paced after the trailing
 * quote.
 *
 * Output:
 * ppCStr Pointer to the parsed string - must be freed by caller and
 *        does NOT include the quotes.
 * rgerhards, 2005-09-19
 */
rsRetVal parsQuotedCStr(rsParsObj *pThis, rsCStrObj **ppCStr)
{
	register unsigned char *pC;
	rsCStrObj *pCStr;
	rsRetVal iRet;

	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	if((iRet = parsSkipAfterChar(pThis, '"')) != RS_RET_OK)
		return iRet;
	pC = rsCStrGetBufBeg(pThis->pCStr) + pThis->iCurrPos;

	/* OK, we most probably can obtain a value... */
	if((pCStr = rsCStrConstruct()) == NULL)
		return RS_RET_OUT_OF_MEMORY;

	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr)) {
		if(*pC == '"') {
			break;	/* we are done! */
		} else if(*pC == '\\') {
			++pThis->iCurrPos;
			++pC;
			if(pThis->iCurrPos < rsCStrLen(pThis->pCStr)) {
				/* in this case, we copy the escaped character
				 * to the output buffer (but do not rely on this,
				 * we might later introduce other things, like \007!
				 */
				if((iRet = rsCStrAppendChar(pCStr, *pC)) != RS_RET_OK) {
					RSFREEOBJ(pCStr);
					return(iRet);
				}
			}
		} else { /* regular character */
			if((iRet = rsCStrAppendChar(pCStr, *pC)) != RS_RET_OK) {
				RSFREEOBJ(pCStr);
				return(iRet);
			}
		}
		++pThis->iCurrPos;
		++pC;
	}
	
	if(*pC == '"') {
		++pThis->iCurrPos; /* 'eat' trailing quote */
	} else {
		/* error - improperly quoted string! */
		RSFREEOBJ(pCStr);
		return RS_RET_MISSING_TRAIL_QUOTE;
	}

	/* We got the string, let's finish it...  */
	if((iRet = rsCStrFinish(pCStr)) != RS_RET_OK) {
		RSFREEOBJ(pCStr);
		return(iRet);
	}

	/* done! */
	*ppCStr = pCStr;
	return RS_RET_OK;
}

/* 
 * Parsing routine for IPv4, IPv6 and domain name wildcards.
 * 
 * Parses string in the format <addr>[/bits] where 
 * addr can be a IPv4 address (e.g.: 127.0.0.1), IPv6 address (e.g.: [::1]),
 * full hostname (e.g.: localhost.localdomain) or hostname wildcard
 * (e.g.: *.localdomain).
 */
#ifdef SYSLOG_INET
rsRetVal parsAddrWithBits(rsParsObj *pThis, struct NetAddr **pIP, int *pBits)
{
	register unsigned char *pC;
	unsigned char *pszIP;
	uchar *pszTmp;
	struct addrinfo hints, *res = NULL;
	rsCStrObj *pCStr;
	rsRetVal iRet;

	rsCHECKVALIDOBJECT(pThis, OIDrsPars);
	assert(pIP != NULL);
	assert(pBits != NULL);

	if((pCStr = rsCStrConstruct()) == NULL)
		return RS_RET_OUT_OF_MEMORY;

	parsSkipWhitespace(pThis);
	pC = rsCStrGetBufBeg(pThis->pCStr) + pThis->iCurrPos;

	/* we parse everything until either '/', ',' or
	 * whitespace. Validity will be checked down below.
	 */
	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr)
	      && *pC != '/' && *pC != ',' && !isspace((int)*pC)) {
		if((iRet = rsCStrAppendChar(pCStr, *pC)) != RS_RET_OK) {
			RSFREEOBJ(pCStr);
			return(iRet);
		}
		++pThis->iCurrPos;
		++pC;
	}
	
	/* We got the string, let's finish it...  */
	if((iRet = rsCStrFinish(pCStr)) != RS_RET_OK) {
		RSFREEOBJ(pCStr);
		return(iRet);
	}

	/* now we have the string and must check/convert it to
	 * an NetAddr structure.
	 */	
  	if((pszIP = rsCStrConvSzStrAndDestruct(pCStr)) == NULL)
		return RS_RET_ERR;

	*pIP = malloc (sizeof (struct NetAddr));
	memset (*pIP, 0, sizeof (struct NetAddr));
	
	if (*((char*)pszIP) == '[') {
		pszTmp = (uchar*)strchr ((char*)pszIP, ']');
		if (pszTmp == NULL) {
			free (pszIP);
			return RS_RET_INVALID_IP;
		}
		*pszTmp = '\0';

		memset (&hints, 0, sizeof (struct addrinfo));
		hints.ai_family = AF_INET6;
		hints.ai_flags  = AI_ADDRCONFIG | AI_NUMERICHOST;
		
		switch(getaddrinfo ((char*)pszIP+1, NULL, &hints, &res)) {
		case 0: 
			(*pIP)->addr.NetAddr = malloc (res->ai_addrlen);
			memcpy ((*pIP)->addr.NetAddr, res->ai_addr, res->ai_addrlen);
			freeaddrinfo (res);
			break;
		case EAI_NONAME:
			F_SET((*pIP)->flags, ADDR_NAME|ADDR_PRI6);
			(*pIP)->addr.HostWildcard = strdup ((const char*)pszIP+1);
			break;
		default:
			free (pszIP);
			free (*pIP);
			return RS_RET_ERR;
		}
		
		if(*pC == '/') {
			/* mask bits follow, let's parse them! */
			++pThis->iCurrPos; /* eat slash */
			if((iRet = parsInt(pThis, pBits)) != RS_RET_OK) {
				free (pszIP);
				free (*pIP);
				return(iRet);
			}
			/* we need to refresh pointer (changed by parsInt()) */
			pC = rsCStrGetBufBeg(pThis->pCStr) + pThis->iCurrPos;
		} else {
			/* no slash, so we assume a single host (/128) */
			*pBits = 128;
		}
	} else { /* now parse IPv4 */
		memset (&hints, 0, sizeof (struct addrinfo));
		hints.ai_family = AF_INET;
		hints.ai_flags  = AI_ADDRCONFIG | AI_NUMERICHOST;
		
		switch(getaddrinfo ((char*)pszIP, NULL, &hints, &res)) {
		case 0: 
			(*pIP)->addr.NetAddr = malloc (res->ai_addrlen);
			memcpy ((*pIP)->addr.NetAddr, res->ai_addr, res->ai_addrlen);
			freeaddrinfo (res);
			break;
		case EAI_NONAME:
			F_SET((*pIP)->flags, ADDR_NAME);
			(*pIP)->addr.HostWildcard = strdup ((const char*)pszIP);
			break;
		default:
			free (pszIP);
			free (*pIP);
			return RS_RET_ERR;
		}
			
		if(*pC == '/') {
			/* mask bits follow, let's parse them! */
			++pThis->iCurrPos; /* eat slash */
			if((iRet = parsInt(pThis, pBits)) != RS_RET_OK) {
				free (pszIP);
				free (*pIP);
				return(iRet);
			}
			/* we need to refresh pointer (changed by parsInt()) */
			pC = rsCStrGetBufBeg(pThis->pCStr) + pThis->iCurrPos;
		} else {
			/* no slash, so we assume a single host (/32) */
			*pBits = 32;
		}
	}
	free(pszIP); /* no longer needed */

	/* skip to next processable character */
	while(pThis->iCurrPos < rsCStrLen(pThis->pCStr)
	      && (*pC == ',' || isspace((int)*pC))) {
		++pThis->iCurrPos;
		++pC;
	}

	return RS_RET_OK;
}
#endif  /* #ifdef SYSLOG_INET */


/* tell if the parsepointer is at the end of the
 * to-be-parsed string. Returns 1, if so, 0
 * otherwise. rgerhards, 2005-09-27
 */
int parsIsAtEndOfParseString(rsParsObj *pThis)
{
	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	return (pThis->iCurrPos < rsCStrLen(pThis->pCStr)) ? 0 : 1;
}


/* return the position of the parse pointer
 */
int rsParsGetParsePointer(rsParsObj *pThis)
{
	rsCHECKVALIDOBJECT(pThis, OIDrsPars);

	if(pThis->iCurrPos < rsCStrLen(pThis->pCStr))
		return pThis->iCurrPos;
	else
		return rsCStrLen(pThis->pCStr) - 1;
}

/* peek at the character at the parse pointer
 * the caller must ensure that the parse pointer is not
 * at the end of the parse buffer (e.g. by first calling
 * parsIsAtEndOfParseString).
 * rgerhards, 2005-09-27
 */
char parsPeekAtCharAtParsPtr(rsParsObj *pThis)
{
	rsCHECKVALIDOBJECT(pThis, OIDrsPars);
	assert(pThis->iCurrPos < rsCStrLen(pThis->pCStr));
	
	return(*(pThis->pCStr->pBuf + pThis->iCurrPos));
}

/* return the current position inside the parse object.
 * rgerhards, 2007-07-04
 */
int parsGetCurrentPosition(rsParsObj *pThis)
{
	return pThis->iCurrPos;
}


/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 * vi:set ai:
 */
