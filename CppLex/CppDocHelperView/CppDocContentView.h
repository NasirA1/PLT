#pragma once


// CCppDocContentView view

class CCppDocContentView : public CRichEditView
{
	DECLARE_DYNCREATE(CCppDocContentView)

protected:
	CCppDocContentView();           // protected constructor used by dynamic creation
	virtual ~CCppDocContentView();

public:
#ifdef _DEBUG
	virtual void AssertValid() const;
#ifndef _WIN32_WCE
	virtual void Dump(CDumpContext& dc) const;
#endif
#endif

protected:
	DECLARE_MESSAGE_MAP()
};


