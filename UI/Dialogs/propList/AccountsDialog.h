#pragma once
#include <UI/Dialogs/BaseDialog.h>
#include <UI/Controls/StyleTree/StyleTreeCtrl.h>
#include <core/sortlistdata/sortListData.h>
#include <core/mapi/account/accountHelper.h>

class CContentsTableListCtrl;

namespace dialog
{
	class AccountsDialog : public CBaseDialog
	{
	public:
		AccountsDialog(
			_In_ ui::CParentWnd* pParentWnd,
			_In_ std::shared_ptr<cache::CMapiObjects> lpMapiObjects,
			_In_opt_ LPMAPISESSION lpMAPISession);
		~AccountsDialog();

	protected:
		// Overrides from base class
		BOOL OnInitDialog() override;

	private:
		controls::StyleTreeCtrl m_lpAccountsList{};
		LPMAPISESSION m_lpMAPISession{};
		LPOLKACCOUNTMANAGER m_lpAcctMgr{};
		CAccountHelper* m_lpMyAcctHelper{};
		std::wstring m_lpwszProfile;

		void InitAccountManager();
		void EnumAccounts(const std::wstring& szCat, const CLSID* pclsidCategory);
		void EnumCategories();
		DECLARE_MESSAGE_MAP()
	};
} // namespace dialog