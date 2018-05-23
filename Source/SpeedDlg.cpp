/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** 0CC-FamiTracker is (C) 2014-2018 HertzDevil
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

#include "SpeedDlg.h"
#include "APU/Types.h"		// // //

// CSpeedDlg dialog

IMPLEMENT_DYNAMIC(CSpeedDlg, CDialog)
CSpeedDlg::CSpeedDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CSpeedDlg::IDD, pParent), m_iSpeed(0)
{
}

CSpeedDlg::~CSpeedDlg()
{
}

void CSpeedDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CSpeedDlg, CDialog)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDCANCEL, OnBnClickedCancel)
END_MESSAGE_MAP()


// CSpeedDlg message handlers


int CSpeedDlg::GetSpeedFromDlg(int InitialSpeed)
{
	m_iSpeed = InitialSpeed;
	CDialog::DoModal();
	return m_iSpeed;
}

void CSpeedDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
	m_iSpeed = ((CSliderCtrl*)pScrollBar)->GetPos();
	SetDlgItemTextW(IDC_SPEED, FormattedW(L"%i Hz", m_iSpeed));
}

BOOL CSpeedDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	CSliderCtrl *Slider = static_cast<CSliderCtrl*>(GetDlgItem(IDC_SPEED_SLD));

	Slider->SetRange(FRAME_RATE_MIN, FRAME_RATE_MAX);
	Slider->SetPos(m_iSpeed);

	SetDlgItemTextW(IDC_SPEED, FormattedW(L"%i Hz", m_iSpeed));

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

void CSpeedDlg::OnBnClickedCancel()
{
	m_iSpeed = 0;
	OnCancel();
}
