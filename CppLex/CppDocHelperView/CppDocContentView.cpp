// CppDockHelperView.cpp : implementation file
//

#include "stdafx.h"
#include "CppDocContentView.h"


// CCppDocContentView

IMPLEMENT_DYNCREATE(CCppDocContentView, CRichEditView)

CCppDocContentView::CCppDocContentView()
{

}

CCppDocContentView::~CCppDocContentView()
{
}

BEGIN_MESSAGE_MAP(CCppDocContentView, CRichEditView)
END_MESSAGE_MAP()


// CCppDocContentView diagnostics

#ifdef _DEBUG
void CCppDocContentView::AssertValid() const
{
	CRichEditView::AssertValid();
}

#ifndef _WIN32_WCE
void CCppDocContentView::Dump(CDumpContext& dc) const
{
	CRichEditView::Dump(dc);
}
#endif
#endif //_DEBUG


// CCppDocContentView message handlers
