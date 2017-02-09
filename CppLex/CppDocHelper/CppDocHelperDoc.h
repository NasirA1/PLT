
// CppDocHelperDoc.h : interface of the CCppDocHelperDoc class
//


#pragma once


class CCppDocHelperDoc : public CDocument
{
protected: // create from serialization only
	CCppDocHelperDoc();
	DECLARE_DYNCREATE(CCppDocHelperDoc)

// Attributes
public:

// Operations
public:

// Overrides
public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);


// Implementation
public:
	virtual ~CCppDocHelperDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif


// Generated message map functions
protected:
	DECLARE_MESSAGE_MAP()
};
