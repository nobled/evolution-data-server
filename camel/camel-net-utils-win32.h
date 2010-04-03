/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER within this package.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_NET_UTILS_WIN32_H
#define CAMEL_NET_UTILS_WIN32_H

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ws2tcpip.h>

#define _COMPAT_STRCPY_S(_Dst,_Size,_Src) strcpy((_Dst),(_Src))
#define _COMPAT_STRCAT_S(_Dst,_Size,_Src) strcat((_Dst),(_Src))
#define _COMPAT_STRNCPY_S(_Dst,_Size,_Src,_Count) strncpy((_Dst),(_Src),(_Count)); (_Dst)[(_Size) - 1] = 0
#define _COMPAT_SPRINTF_S_1(_Dst,_Size,_Format,_Arg1) sprintf((_Dst),(_Format),(_Arg1))

#ifndef _COMPAT_COUNTOF
#ifndef __cplusplus
#define _COMPAT_COUNTOF(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#else
template <typename __CountofType,size_t _N> char (&__COMPAT_countof_helper(__CountofType (&_Array)[_N]))[_N];
#define _COMPAT_COUNTOF(_Array) sizeof(__COMPAT_countof_helper(_Array))
#endif
#endif

#define CompatMalloc(tSize) calloc(1,(tSize))
#define CompatFree(p) free(p)
#define CompatSwap(a,b,c) { (c) = (a); (a) = (b); (b) = (c); }
#define getaddrinfo CompatGetAddrInfo
#define getnameinfo CompatGetNameInfo
#define freeaddrinfo CompatFreeAddrInfo

typedef int (WINAPI *COMPAT_PGETADDRINFO)(const char *nodename,const char *servname,const struct addrinfo *hints,struct addrinfo **res);
typedef int (WINAPI *COMPAT_PGETNAMEINFO)(const struct sockaddr *sa,socklen_t salen,char *host,size_t hostlen,char *serv,size_t servlen,int flags);
typedef void (WINAPI *COMPAT_PFREEADDRINFO)(struct addrinfo *ai);

G_BEGIN_DECLS

  static char *WINAPI CompatStrdup (const char *pszString) {
    char *pszMemory;
    size_t cchMemory;
    if(!pszString) return(NULL);
    cchMemory = strlen(pszString) + 1;
    pszMemory = (char *) CompatMalloc(cchMemory);
    if(!pszMemory) return(NULL);
    _COMPAT_STRCPY_S(pszMemory,cchMemory,pszString);
    return pszMemory;
  }

  static WINBOOL WINAPI CompatParseV4Address (const char *pszAddress,PDWORD pdwAddress) {
    DWORD dwAddress = 0;
    const char *pcNext = NULL;
    int iCount = 0;
    for(pcNext = pszAddress;*pcNext!='\0';pcNext++)
      if(*pcNext=='.') iCount++;
    if(iCount!=3) return FALSE;
    dwAddress = inet_addr(pszAddress);
    if(dwAddress==INADDR_NONE) return FALSE;
    *pdwAddress = dwAddress;
    return TRUE;
  }

  static struct addrinfo *WINAPI CompatNewAddrInfo (int iSocketType,int iProtocol,WORD wPort,DWORD dwAddress) {
    struct addrinfo *ptNew;
    struct sockaddr_in *ptAddress;
    ptNew = (struct addrinfo *) CompatMalloc(sizeof(struct addrinfo));
    if(!ptNew) return NULL;
    ptAddress = (struct sockaddr_in *) CompatMalloc(sizeof(struct sockaddr_in));
    if(!ptAddress) {
      CompatFree(ptNew);
      return NULL;
    }
    ptAddress->sin_family = AF_INET;
    ptAddress->sin_port = wPort;
    ptAddress->sin_addr.s_addr = dwAddress;
    ptNew->ai_family = PF_INET;
    ptNew->ai_socktype = iSocketType;
    ptNew->ai_protocol = iProtocol;
    ptNew->ai_addrlen = sizeof(struct sockaddr_in);
    ptNew->ai_addr = (struct sockaddr *) ptAddress;
    return ptNew;
  }

  static int WINAPI CompatQueryDNS(const char *pszNodeName,int iSocketType,int iProtocol,WORD wPort,char pszAlias[NI_MAXHOST],struct addrinfo **pptResult) {
    struct addrinfo **pptNext = pptResult;
    struct hostent *ptHost = NULL;
    char **ppAddresses;
    *pptNext = NULL;
    pszAlias[0] = '\0';
    ptHost = gethostbyname(pszNodeName);
    if(ptHost) {
      if((ptHost->h_addrtype==AF_INET) && (ptHost->h_length==sizeof(struct in_addr))) {
	for(ppAddresses = ptHost->h_addr_list;*ppAddresses!=NULL;ppAddresses++) {
	  *pptNext = CompatNewAddrInfo(iSocketType,iProtocol,wPort,((struct in_addr *) *ppAddresses)->s_addr);
	  if(!*pptNext) return EAI_MEMORY;
	  pptNext = &((*pptNext)->ai_next);
	}
      }
      _COMPAT_STRNCPY_S(pszAlias,NI_MAXHOST,ptHost->h_name,NI_MAXHOST - 1);
      return 0;
    }
    switch(WSAGetLastError()) {
		case WSAHOST_NOT_FOUND: return EAI_NONAME;
		case WSATRY_AGAIN: return EAI_AGAIN;
		case WSANO_RECOVERY: return EAI_FAIL;
		case WSANO_DATA: return EAI_NODATA;
		default: return EAI_NONAME;
    }
  }

  static int WINAPI CompatLookupNode(const char *pszNodeName,int iSocketType,int iProtocol,WORD wPort,WINBOOL bAI_CANONNAME,struct addrinfo **pptResult) {
    int iError = 0;
    int iAliasCount = 0;
    char szFQDN1[NI_MAXHOST] = "";
    char szFQDN2[NI_MAXHOST] = "";
    char *pszName = szFQDN1;
    char *pszAlias = szFQDN2;
    char *pszScratch = NULL;
    _COMPAT_STRNCPY_S(pszName,NI_MAXHOST,pszNodeName,NI_MAXHOST - 1);
    for(;;) {
      iError = CompatQueryDNS(pszNodeName,iSocketType,iProtocol,wPort,pszAlias,pptResult);
      if(iError) break;
      if(*pptResult) break;
      if((!strlen(pszAlias)) || (!strcmp(pszName,pszAlias)) || (++iAliasCount==16)) {
	iError = EAI_FAIL;
	break;
      }
      CompatSwap(pszName,pszAlias,pszScratch);
    }
    if(!iError && bAI_CANONNAME) {
      (*pptResult)->ai_canonname = CompatStrdup(pszAlias);
      if(!(*pptResult)->ai_canonname) iError = EAI_MEMORY;
    }
    return iError;
  }

  static int WINAPI CompatClone (WORD wPort,struct addrinfo *ptResult) {
    struct addrinfo *ptNext = NULL;
    struct addrinfo *ptNew = NULL;
    for(ptNext = ptResult; ptNext!=NULL;) {
      ptNew = CompatNewAddrInfo(SOCK_DGRAM,ptNext->ai_protocol,wPort,((struct sockaddr_in *) ptNext->ai_addr)->sin_addr.s_addr);
      if(!ptNew) break;
      ptNew->ai_next = ptNext->ai_next;
      ptNext->ai_next = ptNew;
      ptNext = ptNew->ai_next;
    }
    if(ptNext!=NULL) return EAI_MEMORY;
    return 0;
  }


  static void WINAPI CompatLegacyFreeAddrInfo (struct addrinfo *ptHead) {
    struct addrinfo *ptNext;
    for(ptNext = ptHead;ptNext!=NULL;ptNext = ptHead) {
      if(ptNext->ai_canonname) CompatFree(ptNext->ai_canonname);
      if(ptNext->ai_addr) CompatFree(ptNext->ai_addr);
      ptHead = ptNext->ai_next;
      CompatFree(ptNext);
    }
  }

  static int WINAPI CompatLegacyGetAddrInfo(const char *pszNodeName,const char *pszServiceName,const struct addrinfo *ptHints,struct addrinfo **pptResult) {
    int iError = 0;
    int iFlags = 0;
    int iFamily = PF_UNSPEC;
    int iSocketType = 0;
    int iProtocol = 0;
    WORD wPort = 0;
    DWORD dwAddress = 0;
    struct servent *ptService = NULL;
    char *pc = NULL;
    WINBOOL bClone = FALSE;
    WORD wTcpPort = 0;
    WORD wUdpPort = 0;
    *pptResult = NULL;
    if((!pszNodeName) && (!pszServiceName)) return EAI_NONAME;
    if(ptHints) {
      if((ptHints->ai_addrlen!=0) || (ptHints->ai_canonname!=NULL) || (ptHints->ai_addr!=NULL) || (ptHints->ai_next!=NULL)) return EAI_FAIL;
      iFlags = ptHints->ai_flags;
      if((iFlags & AI_CANONNAME) && !pszNodeName) return EAI_BADFLAGS;
      iFamily = ptHints->ai_family;
      if((iFamily!=PF_UNSPEC) && (iFamily!=PF_INET)) return EAI_FAMILY;
      iSocketType = ptHints->ai_socktype;
      if((iSocketType!=0) && (iSocketType!=SOCK_STREAM) && (iSocketType!=SOCK_DGRAM) && (iSocketType!=SOCK_RAW)) return EAI_SOCKTYPE;
      iProtocol = ptHints->ai_protocol;
    }
    if(pszServiceName) {
      wPort = (WORD) strtoul(pszServiceName,&pc,10);
      if(*pc=='\0') {
	wPort = wTcpPort = wUdpPort = htons(wPort);
	if(iSocketType==0) {
	  bClone = TRUE;
	  iSocketType = SOCK_STREAM;
	}
      } else {
	if((iSocketType==0) || (iSocketType==SOCK_DGRAM)) {
	  ptService = getservbyname(pszServiceName,"udp");
	  if(ptService) wPort = wUdpPort = ptService->s_port;
	}
	if((iSocketType==0) || (iSocketType==SOCK_STREAM)) {
	  ptService = getservbyname(pszServiceName,"tcp");
	  if(ptService) wPort = wTcpPort = ptService->s_port;
	}
	if(wPort==0) return (iSocketType ? EAI_SERVICE : EAI_NONAME);
	if(iSocketType==0) {
	  iSocketType = (wTcpPort) ? SOCK_STREAM : SOCK_DGRAM;
	  bClone = (wTcpPort && wUdpPort);
	}
      }
    }
    if((!pszNodeName) || (CompatParseV4Address(pszNodeName,&dwAddress))) {
      if(!pszNodeName) {
	dwAddress = htonl((iFlags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
      }
      *pptResult = CompatNewAddrInfo(iSocketType,iProtocol,wPort,dwAddress);
      if(!(*pptResult)) iError = EAI_MEMORY;
      if(!iError && pszNodeName) {
	(*pptResult)->ai_flags |= AI_NUMERICHOST;
	if(iFlags & AI_CANONNAME) {
	  (*pptResult)->ai_canonname =
	    CompatStrdup(inet_ntoa(*((struct in_addr *) &dwAddress)));
	  if(!(*pptResult)->ai_canonname) iError = EAI_MEMORY;
	}
      }
    } else if(iFlags & AI_NUMERICHOST) iError = EAI_NONAME;
    else iError = CompatLookupNode(pszNodeName,iSocketType,iProtocol,wPort,(iFlags & AI_CANONNAME),pptResult);
    if(!iError && bClone) iError = CompatClone(wUdpPort,*pptResult);
    if(iError) {
      CompatLegacyFreeAddrInfo(*pptResult);
      *pptResult = NULL;
    }
    return (iError);
  }

  static int WINAPI CompatLegacyGetNameInfo(const struct sockaddr *ptSocketAddress,socklen_t tSocketLength,char *pszNodeName,size_t tNodeLength,char *pszServiceName,size_t tServiceLength,int iFlags) {
    struct servent *ptService;
    WORD wPort;
    char szBuffer[] = "65535";
    char *pszService = szBuffer;
    struct hostent *ptHost;
    struct in_addr tAddress;
    char *pszNode = NULL;
    char *pc = NULL;

    if((!ptSocketAddress) || (tSocketLength < sizeof(struct sockaddr))) return EAI_FAIL;
    if(ptSocketAddress->sa_family!=AF_INET) return EAI_FAMILY;
    if(tSocketLength < sizeof(struct sockaddr_in)) return EAI_FAIL;
    if(!(pszNodeName && tNodeLength) && !(pszServiceName && tServiceLength)) return EAI_NONAME;
    if((iFlags & NI_NUMERICHOST) && (iFlags & NI_NAMEREQD)) return EAI_BADFLAGS;
    if(pszServiceName && tServiceLength) {
      wPort = ((struct sockaddr_in *) ptSocketAddress)->sin_port;
      if(iFlags & NI_NUMERICSERV) {
	_COMPAT_SPRINTF_S_1(szBuffer,_COMPAT_COUNTOF(szBuffer),"%u",ntohs(wPort));
      } else {
	ptService = getservbyport(wPort,(iFlags & NI_DGRAM) ? "udp" : NULL);
	if(ptService && ptService->s_name) {
	  pszService = ptService->s_name;
	} else {
	  _COMPAT_SPRINTF_S_1(szBuffer,_COMPAT_COUNTOF(szBuffer),"%u",ntohs(wPort));
	}
      }
      if(tServiceLength > strlen(pszService))
	_COMPAT_STRCPY_S(pszServiceName,tServiceLength,pszService);
      else return EAI_FAIL;
    }
    if(pszNodeName && tNodeLength) {
      tAddress = ((struct sockaddr_in *) ptSocketAddress)->sin_addr;
      if(iFlags & NI_NUMERICHOST) {
	pszNode = inet_ntoa(tAddress);
      } else {
	ptHost = gethostbyaddr((char *) &tAddress,sizeof(struct in_addr),AF_INET);
	if(ptHost && ptHost->h_name) {
	  pszNode = ptHost->h_name;
	  if((iFlags & NI_NOFQDN) && ((pc = strchr(pszNode,'.'))!=NULL)) *pc = '\0';
	} else {
	  if(iFlags & NI_NAMEREQD) {
	    switch(WSAGetLastError()) {
						case WSAHOST_NOT_FOUND: return EAI_NONAME;
						case WSATRY_AGAIN: return EAI_AGAIN;
						case WSANO_RECOVERY: return EAI_FAIL;
						default: return EAI_NONAME;
	    }
	  } else pszNode = inet_ntoa(tAddress);
	}
      }
      if(tNodeLength > strlen(pszNode)) _COMPAT_STRCPY_S(pszNodeName,tNodeLength,pszNode);
      else return EAI_FAIL;
    }
    return 0;
  }

  typedef struct {
    char const *pszName;
    FARPROC pfAddress;
  } COMPAT_FUNCTION;

#define COMPAT_FUNCTION_ARRAY { "getaddrinfo",(FARPROC) CompatLegacyGetAddrInfo,"getnameinfo",(FARPROC) CompatLegacyGetNameInfo,"freeaddrinfo",(FARPROC) CompatLegacyFreeAddrInfo,}

  static FARPROC WINAPI CompatLoad(WORD wFunction) {
    HMODULE hLibrary = NULL;
    static WINBOOL bInitialized = FALSE;
    static COMPAT_FUNCTION rgtGlobal[] = COMPAT_FUNCTION_ARRAY;
    static const int iNumGlobal = (sizeof(rgtGlobal) / sizeof(COMPAT_FUNCTION));
    COMPAT_FUNCTION rgtLocal[] = COMPAT_FUNCTION_ARRAY;
    FARPROC fScratch = NULL;
    int i = 0;
    if(bInitialized) return (rgtGlobal[wFunction].pfAddress);
    for(;;) {
      CHAR SystemDir[MAX_PATH + 1];
      CHAR Path[MAX_PATH + 8];
      if(GetSystemDirectoryA(SystemDir,MAX_PATH)==0) break;
      _COMPAT_STRCPY_S(Path,_COMPAT_COUNTOF(Path),SystemDir);
      _COMPAT_STRCAT_S(Path,_COMPAT_COUNTOF(Path),"\\ws2_32");
      hLibrary = LoadLibraryA(Path);
      if(hLibrary!=NULL) {
	fScratch = GetProcAddress(hLibrary,"getaddrinfo");
	if(!fScratch) {
	  FreeLibrary(hLibrary);
	  hLibrary = NULL;
	}
      }
      if(hLibrary!=NULL) break;
      _COMPAT_STRCPY_S(Path,_COMPAT_COUNTOF(Path),SystemDir);
      _COMPAT_STRCAT_S(Path,_COMPAT_COUNTOF(Path),"\\wship6");
      hLibrary = LoadLibraryA(Path);
      if(hLibrary!=NULL) {
	fScratch = GetProcAddress(hLibrary,"getaddrinfo");
	if(!fScratch) {
	  FreeLibrary(hLibrary);
	  hLibrary = NULL;
	}
      }
      break;
    }
    if(hLibrary!=NULL) {
      for(i = 0;i < iNumGlobal;i++) {
	rgtLocal[i].pfAddress = GetProcAddress(hLibrary,rgtLocal[i].pszName);
	if(!(rgtLocal[i].pfAddress)) {
	  FreeLibrary(hLibrary);
	  hLibrary = NULL;
	  break;
	}
      }
      if(hLibrary!=NULL) {
	for(i = 0;i < iNumGlobal;i++)
	  rgtGlobal[i].pfAddress = rgtLocal[i].pfAddress;
      }
    }
    bInitialized = TRUE;
    return (rgtGlobal[wFunction].pfAddress);
  }

  static int WINAPI CompatGetAddrInfo(const char *nodename,const char *servname,const struct addrinfo *hints,struct addrinfo **res) {
    int iError;
    static COMPAT_PGETADDRINFO pfGetAddrInfo = NULL;
    if(!pfGetAddrInfo) pfGetAddrInfo = (COMPAT_PGETADDRINFO) CompatLoad(0);
    iError = (*pfGetAddrInfo)(nodename,servname,hints,res);
    WSASetLastError(iError);
    return iError;
  }

  static int WINAPI CompatGetNameInfo (const struct sockaddr *sa,socklen_t salen,char *host,size_t hostlen,char *serv,size_t servlen,int flags) {
    int iError;
    static COMPAT_PGETNAMEINFO pfGetNameInfo = NULL;
    if(!pfGetNameInfo) pfGetNameInfo = (COMPAT_PGETNAMEINFO) CompatLoad(1);
    iError = (*pfGetNameInfo)(sa,salen,host,hostlen,serv,servlen,flags);
    WSASetLastError(iError);
    return iError;
  }

  static void WINAPI CompatFreeAddrInfo (struct addrinfo *ai) {
    static COMPAT_PFREEADDRINFO pfFreeAddrInfo = NULL;
    if(!pfFreeAddrInfo) pfFreeAddrInfo = (COMPAT_PFREEADDRINFO) CompatLoad(2);
    (*pfFreeAddrInfo)(ai);
  }

G_END_DECLS
#endif
