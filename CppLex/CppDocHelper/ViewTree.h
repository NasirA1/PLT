
#pragma once
#include "C:\Users\Nasir\Documents\GitHub\PLT\CppLex\CppDocHelperLogic\SmartCppDocHelper.h"


/////////////////////////////////////////////////////////////////////////////
// CViewTree window

class CViewTree : public CTreeCtrl
{
// Construction
public:
	CViewTree(SmartCppDocHelper& docHelper);

// Overrides
protected:
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);

// Implementation
public:
	virtual ~CViewTree();

protected:
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnTvnSelchanged(NMHDR *pNMHDR, LRESULT *pResult);

private:
	SmartCppDocHelper& m_docHelper;
};
