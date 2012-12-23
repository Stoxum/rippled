#include "Application.h"

#include "TrustSetTransactor.h"

#include <boost/bind.hpp>

SETUP_LOG();

TER TrustSetTransactor::doApply()
{
	TER			terResult		= tesSUCCESS;
	cLog(lsINFO) << "doTrustSet>";

	const STAmount		saLimitAmount	= mTxn.getFieldAmount(sfLimitAmount);
	const bool			bQualityIn		= mTxn.isFieldPresent(sfQualityIn);
	const bool			bQualityOut		= mTxn.isFieldPresent(sfQualityOut);
	const uint160		uCurrencyID		= saLimitAmount.getCurrency();
	uint160				uDstAccountID	= saLimitAmount.getIssuer();
	const bool			bHigh			= mTxnAccountID > uDstAccountID;		// true, iff current is high account.

	uint32				uQualityIn		= bQualityIn ? mTxn.getFieldU32(sfQualityIn) : 0;
	uint32				uQualityOut		= bQualityIn ? mTxn.getFieldU32(sfQualityOut) : 0;

	if (bQualityIn && QUALITY_ONE == uQualityIn)
		uQualityIn	= 0;

	if (bQualityOut && QUALITY_ONE == uQualityOut)
		uQualityOut	= 0;

	// Check if destination makes sense.

	if (saLimitAmount.isNegative())
	{
		cLog(lsINFO) << "doTrustSet: Malformed transaction: Negatived credit limit.";

		return temBAD_AMOUNT;
	}
	else if (!uDstAccountID || uDstAccountID == ACCOUNT_ONE)
	{
		cLog(lsINFO) << "doTrustSet: Malformed transaction: Destination account not specified.";

		return temDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		cLog(lsINFO) << "doTrustSet: Malformed transaction: Can not extend credit to self.";

		return temDST_IS_SRC;
	}

	SLE::pointer		sleDst			= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		cLog(lsINFO) << "doTrustSet: Delay transaction: Destination account does not exist.";

		return tecNO_DST;
	}

	const STAmount	saSrcXRPBalance	= mTxnAccount->getFieldAmount(sfBalance);
	const uint32	uOwnerCount		= mTxnAccount->getFieldU32(sfOwnerCount);
	// The reserve required to create the line.
	const uint64	uReserveCreate	= mEngine->getLedger()->getReserve(uOwnerCount + 1);

	STAmount			saLimitAllow	= saLimitAmount;
	saLimitAllow.setIssuer(mTxnAccountID);

	SLE::pointer		sleRippleState	= mEngine->entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));
	if (sleRippleState)
	{
		STAmount		saLowBalance;
		STAmount		saLowLimit;
		STAmount		saHighBalance;
		STAmount		saHighLimit;
		uint32			uLowQualityIn;
		uint32			uLowQualityOut;
		uint32			uHighQualityIn;
		uint32			uHighQualityOut;
		const uint160&	uLowAccountID	= !bHigh ? mTxnAccountID : uDstAccountID;
		const uint160&	uHighAccountID	=  bHigh ? mTxnAccountID : uDstAccountID;
		SLE::ref		sleLowAccount	= !bHigh ? mTxnAccount : sleDst;
		SLE::ref		sleHighAccount	=  bHigh ? mTxnAccount : sleDst;

		//
		// Balances
		//

		saLowBalance	= sleRippleState->getFieldAmount(sfBalance);
		saHighBalance	= -saLowBalance;

		//
		// Limits
		//

		sleRippleState->setFieldAmount(!bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);

		saLowLimit	= !bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfLowLimit);
		saHighLimit	=  bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfHighLimit);

		//
		// Quality in
		//

		if (!bQualityIn)
		{
			// Not setting. Just get it.

			uLowQualityIn	= sleRippleState->getFieldU32(sfLowQualityIn);
			uHighQualityIn	= sleRippleState->getFieldU32(sfHighQualityIn);
		}
		else if (uQualityIn)
		{
			// Setting.

			sleRippleState->setFieldU32(!bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

			uLowQualityIn	= !bHigh ? uQualityIn : sleRippleState->getFieldU32(sfLowQualityIn);
			uHighQualityIn	=  bHigh ? uQualityIn : sleRippleState->getFieldU32(sfHighQualityIn);
		}
		else
		{
			// Clearing.

			sleRippleState->makeFieldAbsent(!bHigh ? sfLowQualityIn : sfHighQualityIn);

			uLowQualityIn	= !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityIn);
			uHighQualityIn	=  bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityIn);
		}

		if (QUALITY_ONE == uLowQualityIn)	uLowQualityIn	= 0;
		if (QUALITY_ONE == uHighQualityIn)	uHighQualityIn	= 0;

		//
		// Quality out
		//

		if (!bQualityOut)
		{
			// Not setting. Just get it.

			uLowQualityOut	= sleRippleState->getFieldU32(sfLowQualityOut);
			uHighQualityOut	= sleRippleState->getFieldU32(sfHighQualityOut);
		}
		else if (uQualityOut)
		{
			// Setting.

			sleRippleState->setFieldU32(!bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

			uLowQualityOut	= !bHigh ? uQualityOut : sleRippleState->getFieldU32(sfLowQualityOut);
			uHighQualityOut	=  bHigh ? uQualityOut : sleRippleState->getFieldU32(sfHighQualityOut);
		}
		else
		{
			// Clearing.

			sleRippleState->makeFieldAbsent(!bHigh ? sfLowQualityOut : sfHighQualityOut);

			uLowQualityOut	= !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityOut);
			uHighQualityOut	=  bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityOut);
		}

		if (QUALITY_ONE == uLowQualityOut)	uLowQualityOut	= 0;
		if (QUALITY_ONE == uHighQualityOut)	uHighQualityOut	= 0;

		const bool	bLowReserveSet		= uLowQualityIn || uLowQualityOut || !!saLowLimit || saLowBalance.isPositive();
		const bool	bLowReserveClear	= !bLowReserveSet;

		const bool	bHighReserveSet		= uHighQualityIn || uHighQualityOut || !!saHighLimit || saHighBalance.isPositive();
		const bool	bHighReserveClear	= !bHighReserveSet;

		const bool	bDefault			= bLowReserveClear && bHighReserveClear;

		const uint32	uFlagsIn		= sleRippleState->getFieldU32(sfFlags);
		uint32			uFlagsOut		= uFlagsIn;

		const bool	bLowReserved		= isSetBit(uFlagsIn, lsfLowReserve);
		const bool	bHighReserved		= isSetBit(uFlagsIn, lsfHighReserve);

		bool		bReserveIncrease	= false;

		if (bLowReserveSet && !bLowReserved)
		{
			// Set reserve for low account.

			mEngine->getNodes().ownerCountAdjust(uLowAccountID, 1, sleLowAccount);
			uFlagsOut			|= lsfLowReserve;

			if (!bHigh)
				bReserveIncrease	= true;
		}

		if (bLowReserveClear && bLowReserved)
		{
			// Clear reserve for low account.

			mEngine->getNodes().ownerCountAdjust(uLowAccountID, -1, sleLowAccount);
			uFlagsOut	&= ~lsfLowReserve;
		}

		if (bHighReserveSet && !bHighReserved)
		{
			// Set reserve for high account.

			mEngine->getNodes().ownerCountAdjust(uHighAccountID, 1, sleHighAccount);
			uFlagsOut	|= lsfHighReserve;

			if (bHigh)
				bReserveIncrease	= true;
		}

		if (bHighReserveClear && bHighReserved)
		{
			// Clear reserve for high account.

			mEngine->getNodes().ownerCountAdjust(uHighAccountID, -1, sleHighAccount);
			uFlagsOut	&= ~lsfHighReserve;
		}

		if (uFlagsIn != uFlagsOut)
			sleRippleState->setFieldU32(sfFlags, uFlagsOut);

		if (bDefault)
		{
			// Can delete.

			uint64		uSrcRef;								// <-- Ignored, dirs never delete.

			terResult	= mEngine->getNodes().dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(uLowAccountID), sleRippleState->getIndex(), false);

			if (tesSUCCESS == terResult)
				terResult	= mEngine->getNodes().dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(uHighAccountID), sleRippleState->getIndex(), false);

			mEngine->entryDelete(sleRippleState);

			cLog(lsINFO) << "doTrustSet: Deleting ripple line";
		}
		else if (bReserveIncrease
			&& isSetBit(mParams, tapOPEN_LEDGER)				// Ledger is not final, we can vote no.
			&& saSrcXRPBalance.getNValue() < uReserveCreate)	// Reserve is not scaled by load.
		{
			cLog(lsINFO) << "doTrustSet: Delay transaction: Insufficent reserve to add trust line.";

			// Another transaction could provide XRP to the account and then this transaction would succeed.
			terResult	= tecINSUF_RESERVE_LINE;
		}
		else
		{
			mEngine->entryModify(sleRippleState);

			cLog(lsINFO) << "doTrustSet: Modify ripple line";
		}
	}
	// Line does not exist.
	else if (!saLimitAmount										// Setting default limit.
		&& (!bQualityIn || !uQualityIn)							// Not setting quality in or setting default quality in.
		&& (!bQualityOut || !uQualityOut))						// Not setting quality out or setting default quality out.
	{
		cLog(lsINFO) << "doTrustSet: Redundant: Setting non-existent ripple line to defaults.";

		return tecNO_LINE_REDUNDANT;
	}
	else if (isSetBit(mParams, tapOPEN_LEDGER)					// Ledger is not final, we can vote no.
		&& saSrcXRPBalance.getNValue() < uReserveCreate)		// Reserve is not scaled by load.
	{
		cLog(lsINFO) << "doTrustSet: Delay transaction: Line does not exist. Insufficent reserve to create line.";

		// Another transaction could create the account and then this transaction would succeed.
		terResult	= tecNO_LINE_INSUF_RESERVE;
	}
	else
	{
		// Create a new ripple line.
		sleRippleState	= mEngine->entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		cLog(lsINFO) << "doTrustSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setFieldAmount(sfBalance, STAmount(uCurrencyID, ACCOUNT_ONE));	// Zero balance in currency.
		sleRippleState->setFieldAmount(!bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);
		sleRippleState->setFieldAmount( bHigh ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID, uDstAccountID));

		if (uQualityIn)
			sleRippleState->setFieldU32(!bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

		if (uQualityOut)
			sleRippleState->setFieldU32(!bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

		sleRippleState->setFieldU32(sfFlags, !bHigh ? lsfLowReserve : lsfHighReserve);

		uint64			uSrcRef;							// <-- Ignored, dirs never delete.

		terResult	= mEngine->getNodes().dirAdd(
			uSrcRef,
			Ledger::getOwnerDirIndex(mTxnAccountID),
			sleRippleState->getIndex(),
			boost::bind(&Ledger::ownerDirDescriber, _1, mTxnAccountID));

		if (tesSUCCESS == terResult)
		{
			mEngine->getNodes().ownerCountAdjust(mTxnAccountID, 1, mTxnAccount);

			terResult	= mEngine->getNodes().dirAdd(
				uSrcRef,
				Ledger::getOwnerDirIndex(uDstAccountID),
				sleRippleState->getIndex(),
				boost::bind(&Ledger::ownerDirDescriber, _1, uDstAccountID));
		}
	}

	cLog(lsINFO) << "doTrustSet<";

	return terResult;
}

// vim:ts=4
