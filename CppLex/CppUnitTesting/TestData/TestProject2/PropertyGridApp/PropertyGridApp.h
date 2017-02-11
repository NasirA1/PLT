
// PropertyGridApp.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CPropertyGridAppApp:
// See PropertyGridApp.cpp for the implementation of this class
//

class CPropertyGridAppApp : public CWinApp
{
public:

	//Constructs the application object
	CPropertyGridAppApp();

// Overrides
public:
	virtual BOOL InitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
};

//The one and ony app object
extern CPropertyGridAppApp theApp;