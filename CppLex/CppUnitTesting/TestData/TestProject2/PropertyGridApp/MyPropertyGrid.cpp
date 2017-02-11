// MyPropertyGrid.cpp : implementation file
//

#include "stdafx.h"
#include "PropertyGridApp.h"
#include "MyPropertyGrid.h"
#include "PropertyPage1.h"

// MyPropertyGrid

IMPLEMENT_DYNAMIC(MyPropertyGrid, CMFCPropertyGridCtrl)

MyPropertyGrid::MyPropertyGrid(PropertyPage1& page)
	: m_Page(page)
{
}


MyPropertyGrid::~MyPropertyGrid()
{
}


BEGIN_MESSAGE_MAP(MyPropertyGrid, CMFCPropertyGridCtrl)
END_MESSAGE_MAP()



// MyPropertyGrid message handlers




void MyPropertyGrid::OnPropertyChanged(CMFCPropertyGridProperty* pProp) const
{
	m_Page.SetModified(TRUE);
	return CMFCPropertyGridCtrl::OnPropertyChanged(pProp);
}
