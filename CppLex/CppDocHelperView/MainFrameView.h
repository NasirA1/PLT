// MainFrm.h : interface of the CMainFrame class
//
#pragma once
#include "SmartCppDocHelper.h"
#include "resource.h"


class CWinAppEx;
class CMainFrame;


class SmartCppDocHelperView : public ISmartCppDocHelperView
{
public:
	SmartCppDocHelperView(CMainFrame* mainFrame);

	virtual ~SmartCppDocHelperView();

	virtual std::wstring GetSelectedFolder() override;
	
	virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) override;
	
	virtual void DisplayHeaderContent(const std::wstring& content, const bool enabledState) override;
	
	virtual void DisplaySourceContent(const std::wstring& content, const bool enabledState) override;
	
	virtual std::wstring GetHeaderContent() const override;
	
	virtual std::wstring GetSourceContent() const override;
	
	virtual void DisplayError(const std::wstring& message) override;

	inline SmartCppDocHelper& CppDocHelper() { return m_docHelper; }


	// Register the application's document templates.  Document templates
	//  serve as the connection between documents, frame windows and views
	static BOOL RegisterDocumentTemplates(CWinAppEx& app);


private:
	SmartCppDocHelper m_docHelper;
	CMainFrame* m_pMainFrame;
};



