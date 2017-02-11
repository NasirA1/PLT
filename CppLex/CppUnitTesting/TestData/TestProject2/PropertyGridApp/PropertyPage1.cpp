// PropertyPage1.cpp : implementation file
//

#include "stdafx.h"
#include "PropertyGridApp.h"
#include "PropertyPage1.h"
#include "afxdialogex.h"


// PropertyPage1 dialog

IMPLEMENT_DYNAMIC(PropertyPage1, CPropertyPage)


PropertyPage1::PropertyPage1()
	: CPropertyPage(IDD_PROPPAGE_1)
	, m_wndPropList(*this)
{
}

PropertyPage1::~PropertyPage1()
{
}


void PropertyPage1::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MFCPROPERTYGRID1, m_wndPropList);
}


BEGIN_MESSAGE_MAP(PropertyPage1, CPropertyPage)
END_MESSAGE_MAP()



void PropertyPage1::InitPropGrid()
{
	CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_ObsidianBlack);
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOffice2007));
	m_wndPropList.EnableHeaderCtrl(TRUE, _T("Nibu's Property"), _T("Nibu's Value"));
	m_wndPropList.SetVSDotNetLook(TRUE);

	CMFCPropertyGridProperty* pGroupTest = new CMFCPropertyGridProperty(_T("Group Test"));
	m_wndPropList.AddProperty(pGroupTest);

	const int MaxNesting = 5;
	CMFCPropertyGridProperty* pParent = pGroupTest;
	for (int Index = 0; Index < MaxNesting; ++Index)
	{
		CString Text;
		Text.Format(_T("Nesting %d"), Index + 1);

		CMFCPropertyGridProperty* pParentTemp = new CMFCPropertyGridProperty(Text);

		// Display's a combo with options as True, False, Cool!
		COleVariant Bool((short)VARIANT_FALSE, VT_BOOL);
		pParent->AddSubItem(new CMFCPropertyGridProperty(_T("Bool test"), Bool, _T("Testing kids")));
		pParent->AddSubItem(pParentTemp);
		pParent = pParentTemp;
	}

	// A font property
	LOGFONT lf = { 0 };
	GetFont()->GetLogFont(&lf);
	CMFCPropertyGridFontProperty* pFntProp = new CMFCPropertyGridFontProperty(_T("Font (Font dialog comes up)"), lf);
	pGroupTest->AddSubItem(pFntProp);

	// Combo property, set sub options which are displayed in a combo
	CMFCPropertyGridProperty* pCmbProp = new CMFCPropertyGridProperty(_T("Border (A combo box)"), _T("Dialog Frame"), _T("One of: None, Thin, Resizable, or Dialog Frame"));
	pCmbProp->AddOption(_T("None"));
	pCmbProp->AddOption(_T("Thin"));
	pCmbProp->AddOption(_T("Resizable"));
	pCmbProp->AddOption(_T("Dialog Frame"));
	pCmbProp->AllowEdit(FALSE);
	pGroupTest->AddSubItem(pCmbProp);

	// A folder browse dialog property
	CMFCPropertyGridFileProperty* pFolderProp = new CMFCPropertyGridFileProperty(_T("Select folder (Browse for folder dialog)"), _T("C:\\Windows"));
	pGroupTest->AddSubItem(pFolderProp);

	// A file open dialog property
	CMFCPropertyGridFileProperty* pFileProp = new CMFCPropertyGridFileProperty(_T("Select file (Open file dialog)"), TRUE, _T("C:\\Windows"));
	pGroupTest->AddSubItem(pFileProp);

	// A masked edit control for phone number
	pGroupTest->AddSubItem(new CMFCPropertyGridProperty(_T("Phone (Masked edit)"), _T("(123) 123-12-12"), 
		_T("Enter a phone number"), 0, _T(" ddd  ddd dd dd"), _T("(___) ___-__-__")));

	// A color property
	CMFCPropertyGridColorProperty* pColorProp = new CMFCPropertyGridColorProperty(_T("Select color"), RGB(120, 198, 250));
	pGroupTest->AddSubItem(pColorProp);

	// Set custom colors for property grid
	//m_wndPropList.SetCustomColors(RGB(228, 243, 254), RGB(46, 70, 165), RGB(200, 236, 209), RGB(33, 102, 49), RGB(255, 229, 216), RGB(128, 0, 0), RGB(159, 159, 255));

	//Adjust column widths
	CRect rect;
	m_wndPropList.GetWindowRect(&rect);
	m_wndPropList.PostMessage(WM_SIZE, 0, MAKELONG(rect.Width(), rect.Height()));
}


BOOL PropertyPage1::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	InitPropGrid();

	return TRUE;  // return TRUE unless you set the focus to a control
								// EXCEPTION: OCX Property Pages should return FALSE
}



BOOL PropertyPage1::OnApply()
{
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}
