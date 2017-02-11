#pragma once

// MyPropertyGrid

class PropertyPage1;

//Class MyPropertyGrid represents the property grid in the sample application
class MyPropertyGrid : public CMFCPropertyGridCtrl
{
	DECLARE_DYNAMIC(MyPropertyGrid)

public:
	//Constructs the property grid and adds the given propertypage to it
	MyPropertyGrid(PropertyPage1& page);

	//Destructs the property grid
	virtual ~MyPropertyGrid();

protected:
	//MFC Message Map
	DECLARE_MESSAGE_MAP()
public:
	//OnPropertyChanged handler
	virtual void OnPropertyChanged(CMFCPropertyGridProperty* pProp) const;

private:
	PropertyPage1& m_Page;
};


