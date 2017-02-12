
// CppDocHelper.h : main header file for the CppDocHelper application
//
#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif



// CCppDocHelperApp:
// See CppDocHelper.cpp for the implementation of this class
//

class CCppDocHelperApp : public CWinAppEx
{
public:
	CCppDocHelperApp();


// Overrides
public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();

// Implementation
	UINT  m_nAppLook;
	BOOL  m_bHiColorIcons;

	virtual void PreLoadState();
	virtual void LoadCustomState();
	virtual void SaveCustomState();

	afx_msg void OnAppAbout();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnFileOpen();
};

extern CCppDocHelperApp theApp;
