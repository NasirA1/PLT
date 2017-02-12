#pragma once


// CCppDocHelperView view

class CCppDocHelperView : public CRichEditView
{
	DECLARE_DYNCREATE(CCppDocHelperView)

protected:
	CCppDocHelperView();           // protected constructor used by dynamic creation
	virtual ~CCppDocHelperView();

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


