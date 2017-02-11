#pragma once
#include "afxpropertygridctrl.h"
#include "MyPropertyGrid.h"


// PropertyPage1 dialog
//Class PrpertyPage1 represents 
class PropertyPage1 : public CPropertyPage
{
	DECLARE_DYNAMIC(PropertyPage1)

public:
	//Constructs the PorpertyPage1 object
	PropertyPage1();

	//.dtor
	virtual ~PropertyPage1();


// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PROPPAGE_1 };
#endif

protected:
	// DDX/DDV support
	virtual void DoDataExchange(CDataExchange* pDX);

	//MFC Message Map
	DECLARE_MESSAGE_MAP()

	//Runs upon dialog creation
	virtual BOOL OnInitDialog();

	//Runs when the user clicks the 'Apply' button
	virtual BOOL OnApply();

	//Initialises the property page, adding all properties/subproperties, etc.
	void PropertyPage1::InitPropGrid();


private:
	MyPropertyGrid m_wndPropList;
};
