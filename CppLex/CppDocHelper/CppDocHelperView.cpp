// CppDockHelperView.cpp : implementation file
//

#include "stdafx.h"
#include "CppDocHelperView.h"


// CCppDocHelperView

IMPLEMENT_DYNCREATE(CCppDocHelperView, CRichEditView)

CCppDocHelperView::CCppDocHelperView()
{

}

CCppDocHelperView::~CCppDocHelperView()
{
}

BEGIN_MESSAGE_MAP(CCppDocHelperView, CRichEditView)
END_MESSAGE_MAP()


// CCppDocHelperView diagnostics

#ifdef _DEBUG
void CCppDocHelperView::AssertValid() const
{
	CRichEditView::AssertValid();
}

#ifndef _WIN32_WCE
void CCppDocHelperView::Dump(CDumpContext& dc) const
{
	CRichEditView::Dump(dc);
}
#endif
#endif //_DEBUG


// CCppDocHelperView message handlers
