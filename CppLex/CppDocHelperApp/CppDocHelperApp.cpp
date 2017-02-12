// CppDocHelper.cpp : Defines the class behaviors for the application.
//
#include "stdafx.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "CppDocHelperApp.h"
#include "MainFrameView.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif



//TODO Move to MFCUtil project
HMODULE LoadLibraryFromApplicationDirectory(LPCTSTR lpFileName)
{
	//Get the Application diretory
	TCHAR szFullPath[_MAX_PATH];
	szFullPath[0] = _T('\0');
	if (GetModuleFileName(nullptr, szFullPath, _countof(szFullPath)) == 0)
		return nullptr;

	//Form the new path
	TCHAR szDrive[_MAX_DRIVE];
	szDrive[0] = _T('\0');
	TCHAR szDir[_MAX_DIR];
	szDir[0] = _T('\0');
	_tsplitpath_s(szFullPath, szDrive, sizeof(szDrive) / sizeof(TCHAR), szDir, sizeof(szDir) / sizeof(TCHAR), nullptr, 0, nullptr, 0);
	TCHAR szFname[_MAX_FNAME];
	szFname[0] = _T('\0');
	TCHAR szExt[_MAX_EXT];
	szExt[0] = _T('\0');
	_tsplitpath_s(lpFileName, nullptr, 0, nullptr, 0, szFname, sizeof(szFname) / sizeof(TCHAR), szExt, sizeof(szExt) / sizeof(TCHAR));
	_tmakepath_s(szFullPath, sizeof(szFullPath) / sizeof(TCHAR), szDrive, szDir, szFname, szExt);

	//Delegate to LoadLibrary    
	return LoadLibrary(szFullPath);
}



// CCppDocHelperApp
BEGIN_MESSAGE_MAP(CCppDocHelperApp, CWinAppEx)
	ON_COMMAND(ID_APP_ABOUT, &CCppDocHelperApp::OnAppAbout)
END_MESSAGE_MAP()


// CCppDocHelperApp construction
CCppDocHelperApp::CCppDocHelperApp()
{
	m_bHiColorIcons = TRUE;

	// support Restart Manager
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;

	// TODO: replace application ID string below with unique ID string; recommended
	// format for string is CompanyName.ProductName.SubProduct.VersionInformation
	SetAppID(_T("CppDocHelper.AppID.NoVersion"));
}


// The one and only CCppDocHelperApp object
CCppDocHelperApp theApp;


// CCppDocHelperApp initialization
BOOL CCppDocHelperApp::InitInstance()
{
	//Load the scintilla dll
	m_hSciDLL = LoadLibraryFromApplicationDirectory(_T("SciLexer.dll"));
	if (m_hSciDLL == nullptr)
	{
		AfxMessageBox(_T("Scintilla DLL is not installed, Please download the SciTE editor and copy the SciLexer.dll into this application's directory"));
		return FALSE;
	}

	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);
	CWinAppEx::InitInstance();

	// Initialize OLE libraries
	if (!AfxOleInit())
	{
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	EnableTaskbarInteraction(FALSE);

	// AfxInitRichEdit2() is required to use RichEdit control	
	// AfxInitRichEdit2();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("Local AppWizard-Generated Applications"));
	LoadStdProfileSettings(4);  // Load standard INI file options (including MRU)

	InitContextMenuManager();

	InitKeyboardManager();

	InitTooltipManager();

	CMFCToolTipInfo ttParams;
	ttParams.m_bVislManagerTheme = TRUE;
	theApp.GetTooltipManager()->SetTooltipParams(AFX_TOOLTIP_TYPE_ALL, RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

	//Register Document Templates
	if (!SmartCppDocHelperView::RegisterDocumentTemplates(*this))
		return FALSE;

	// Parse command line for standard shell commands, DDE, file open
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	// Dispatch commands specified on the command line.  Will return FALSE if
	// app was launched with /RegServer, /Register, /Unregserver or /Unregister.
	if (!ProcessShellCommand(cmdInfo))
		return FALSE;

	// The one and only window has been initialized, so show and update it
	m_pMainWnd->ShowWindow(SW_SHOW);
	m_pMainWnd->UpdateWindow();

	return TRUE;
}


int CCppDocHelperApp::ExitInstance()
{
	//TODO: handle additional resources you may have added
	AfxOleTerm(FALSE);

	return CWinAppEx::ExitInstance();
}


#pragma region CAboutDlg dialog used for App About

//CAboutDlg dialog used for App About
class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

#pragma endregion


// App command to run the dialog
void CCppDocHelperApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}


// CCppDocHelperApp customization load/save methods
void CCppDocHelperApp::PreLoadState()
{
	BOOL bNameValid;
	CString strName;
	bNameValid = strName.LoadString(IDS_EDIT_MENU);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EDIT);
	bNameValid = strName.LoadString(IDS_EXPLORER);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EXPLORER);
}


void CCppDocHelperApp::LoadCustomState()
{
}


void CCppDocHelperApp::SaveCustomState()
{
}

