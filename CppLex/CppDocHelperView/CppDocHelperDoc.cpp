
// CppDocHelperDoc.cpp : implementation of the CCppDocHelperDoc class
//

#include "stdafx.h"
#include "CppDocHelperDoc.h"
#include <propkey.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CCppDocHelperDoc

IMPLEMENT_DYNCREATE(CCppDocHelperDoc, CDocument)

BEGIN_MESSAGE_MAP(CCppDocHelperDoc, CDocument)
END_MESSAGE_MAP()


// CCppDocHelperDoc construction/destruction

CCppDocHelperDoc::CCppDocHelperDoc()
{
	// TODO: add one-time construction code here

}

CCppDocHelperDoc::~CCppDocHelperDoc()
{
}

BOOL CCppDocHelperDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	// TODO: add reinitialization code here
	// (SDI documents will reuse this document)

	return TRUE;
}




// CCppDocHelperDoc serialization

void CCppDocHelperDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}



// CCppDocHelperDoc diagnostics

#ifdef _DEBUG
void CCppDocHelperDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CCppDocHelperDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CCppDocHelperDoc commands
