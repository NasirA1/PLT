
// PropertyGridApp.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "PropertyGridApp.h"
#include "PropertyPage1.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CPropertyGridAppApp
BEGIN_MESSAGE_MAP(CPropertyGridAppApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()



CPropertyGridAppApp::CPropertyGridAppApp()
{
}


// The one and only CPropertyGridAppApp object
CPropertyGridAppApp theApp;


// CPropertyGridAppApp initialization
BOOL CPropertyGridAppApp::InitInstance()
{
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	if (!AfxSocketInit())
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}


	AfxEnableControlContainer();

	CShellManager *pShellManager = new CShellManager;
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));
	SetRegistryKey(_T("Local AppWizard-Generated Applications"));

	CPropertySheet propSheet(L"Game Config");
	PropertyPage1 page1;
	propSheet.AddPage(&page1);
	m_pMainWnd = &propSheet;
	INT_PTR nResponse = propSheet.DoModal();

	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}
	else if (nResponse == -1)
	{
		TRACE(traceAppMsg, 0, "Warning: dialog creation failed, so application is terminating unexpectedly.\n");
		TRACE(traceAppMsg, 0, "Warning: if you are using MFC controls on the dialog, you cannot #define _AFX_NO_MFC_CONTROLS_IN_DIALOGS.\n");
	}

	// Delete the shell manager created above.
	if (pShellManager != NULL)
	{
		delete pShellManager;
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

