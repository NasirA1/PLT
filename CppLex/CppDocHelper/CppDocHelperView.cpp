
// CppDocHelperView.cpp : implementation of the CCppDocHelperView class
//

#include "stdafx.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "CppDocHelper.h"
#endif

#include "CppDocHelperDoc.h"
#include "CppDocHelperView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CCppDocHelperView

IMPLEMENT_DYNCREATE(CCppDocHelperView, CFormView)

BEGIN_MESSAGE_MAP(CCppDocHelperView, CFormView)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
END_MESSAGE_MAP()

// CCppDocHelperView construction/destruction

CCppDocHelperView::CCppDocHelperView()
	: CFormView(IDD_CPPDOCHELPER_FORM)
{
	// TODO: add construction code here

}

CCppDocHelperView::~CCppDocHelperView()
{
}

void CCppDocHelperView::DoDataExchange(CDataExchange* pDX)
{
	CFormView::DoDataExchange(pDX);
}

BOOL CCppDocHelperView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CFormView::PreCreateWindow(cs);
}

void CCppDocHelperView::OnInitialUpdate()
{
	CFormView::OnInitialUpdate();
	GetParentFrame()->RecalcLayout();
	ResizeParentToFit();

}

void CCppDocHelperView::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	ClientToScreen(&point);
	OnContextMenu(this, point);
}

void CCppDocHelperView::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}


// CCppDocHelperView diagnostics

#ifdef _DEBUG
void CCppDocHelperView::AssertValid() const
{
	CFormView::AssertValid();
}

void CCppDocHelperView::Dump(CDumpContext& dc) const
{
	CFormView::Dump(dc);
}

CCppDocHelperDoc* CCppDocHelperView::GetDocument() const // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CCppDocHelperDoc)));
	return (CCppDocHelperDoc*)m_pDocument;
}
#endif //_DEBUG


// CCppDocHelperView message handlers
