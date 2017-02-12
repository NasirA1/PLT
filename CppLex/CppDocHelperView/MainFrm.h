// MainFrm.h : interface of the CMainFrame class
//
#pragma once
#include "FileView.h"
#include "CppDocContentView.h"
#include "SmartCppDocHelper.h"


class CMainFrame : public CFrameWndEx, public ISmartCppDocHelperView
{
private:
	CCppDocContentView* m_pLeftView;
	CCppDocContentView* m_pRightView;
	bool m_dockingWindowsInitialised = false;

protected: // create from serialization only
	CMainFrame();
	DECLARE_DYNCREATE(CMainFrame)

// Attributes
protected:
	CSplitterWnd m_wndSplitter;

public:
	/** ISmartCppDocHelperView Implementation **/
	virtual std::wstring GetSelectedFolder() override;
	virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) override;
	virtual void DisplayHeaderContent(const std::wstring& content, const bool enabledState) override;
	virtual void DisplaySourceContent(const std::wstring& content, const bool enabledState) override;
	virtual std::wstring GetHeaderContent() const override;
	virtual std::wstring GetSourceContent() const override;
	virtual void DisplayError(const std::wstring& message) override;

// Operations
public:
	void OnFileOpenCommand();

// Overrides
public:
	virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL LoadFrame(UINT nIDResource, DWORD dwDefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, CWnd* pParentWnd = NULL, CCreateContext* pContext = NULL);

// Implementation
public:
	virtual ~CMainFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:  // control bar embedded members
	SmartCppDocHelper m_docHelper;
	CMFCMenuBar       m_wndMenuBar;
	CMFCToolBar       m_wndToolBar;
	CMFCStatusBar     m_wndStatusBar;
	CMFCToolBarImages m_UserImages;
	CFileView         m_wndFileView;

// Generated message map functions
protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnViewCustomize();
	afx_msg LRESULT OnToolbarCreateNew(WPARAM wp, LPARAM lp);
	afx_msg void OnApplicationLook(UINT id);
	afx_msg void OnUpdateApplicationLook(CCmdUI* pCmdUI);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()

	BOOL CreateDockingWindows();
	void SetDockingWindowIcons(BOOL bHiColorIcons);
};

