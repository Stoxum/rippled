//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <stoxum/app/paths/Flow.h>
#include <stoxum/app/paths/impl/Steps.h>
#include <stoxum/basics/contract.h>
#include <stoxum/core/Config.h>
#include <stoxum/ledger/ApplyViewImpl.h>
#include <stoxum/ledger/PaymentSandbox.h>
#include <stoxum/ledger/Sandbox.h>
#include <stoxum/protocol/Feature.h>
#include <stoxum/protocol/JsonFields.h>
#include <test/jtx.h>
#include <test/jtx/PathSet.h>

namespace ripple {
namespace test {

struct DepositAuth_test : public beast::unit_test::suite
{
    // Helper function that returns the reserve on an account based on
    // the passed in number of owners.
    static XRPAmount reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve (count);
    }

    // Helper function that returns true if acct has the lsfDepostAuth flag set.
    static bool hasDepositAuth (jtx::Env const& env, jtx::Account const& acct)
    {
        return ((*env.le(acct))[sfFlags] & lsfDepositAuth) == lsfDepositAuth;
    };


    void testEnable()
    {
        testcase ("Enable");

        using namespace jtx;
        Account const alice {"alice"};

        {
            // featureDepositAuth is disabled.
            Env env (*this, supported_amendments() - featureDepositAuth);
            env.fund (STM (10000), alice);

            // Note that, to support old behavior, invalid flags are ignored.
            env (fset (alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT (! hasDepositAuth (env, alice));

            env (fclear (alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT (! hasDepositAuth (env, alice));
        }
        {
            // featureDepositAuth is enabled.
            Env env (*this);
            env.fund (STM (10000), alice);

            env (fset (alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT (hasDepositAuth (env, alice));

            env (fclear (alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT (! hasDepositAuth (env, alice));
        }
    }

    void testPayIOU()
    {
        // Exercise IOU payments and non-direct STM payments to an account
        // that has the lsfDepositAuth flag set.
        testcase ("Pay IOU");

        using namespace jtx;
        Account const alice {"alice"};
        Account const bob {"bob"};
        Account const carol {"carol"};
        Account const gw {"gw"};
        IOU const USD = gw["USD"];

        Env env (*this);

        env.fund (STM (10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob);
        env.close();

        env (pay (gw, alice, USD (150)));
        env (offer (carol, USD(100), STM(100)));
        env.close();

        // Make sure bob's trust line is all set up so he can receive USD.
        env (pay (alice, bob, USD (50)));
        env.close();

        // bob sets the lsfDepositAuth flag.
        env (fset (bob, asfDepositAuth), require(flags (bob, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedIouPayments = [this, &env, &alice, &bob, &USD] ()
        {
            env.require (flags (bob, asfDepositAuth));

            // Capture bob's balances before hand to confirm they don't change.
            PrettyAmount const bobXrpBalance {env.balance (bob, STM)};
            PrettyAmount const bobUsdBalance {env.balance (bob, USD)};

            env (pay (alice, bob, USD (50)), ter (tecNO_PERMISSION));
            env.close();

            // Note that even though alice is paying bob in STM, the payment
            // is still not allowed since the payment passes through an offer.
            env (pay (alice, bob, drops(1)),
                sendmax (USD (1)), ter (tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT (bobXrpBalance == env.balance (bob, STM));
            BEAST_EXPECT (bobUsdBalance == env.balance (bob, USD));
        };

        //  Test when bob has an STM balance > base reserve.
        failedIouPayments();

        // Set bob's STM balance == base reserve.  Also demonstrate that
        // bob can make payments while his lsfDepositAuth flag is set.
        env (pay (bob, alice, USD(25)));
        env.close();

        {
            STAmount const bobPaysXRP {
                env.balance (bob, STM) - reserve (env, 1)};
            XRPAmount const bobPaysFee {reserve (env, 1) - reserve (env, 0)};
            env (pay (bob, alice, bobPaysXRP), fee (bobPaysFee));
            env.close();
        }

        // Test when bob's STM balance == base reserve.
        BEAST_EXPECT (env.balance (bob, STM) == reserve (env, 0));
        BEAST_EXPECT (env.balance (bob, USD) == USD(25));
        failedIouPayments();

        // Test when bob has an STM balance == 0.
        env (noop (bob), fee (reserve (env, 0)));
        env.close ();

        BEAST_EXPECT (env.balance (bob, STM) == STM (0));
        failedIouPayments();

        // Give bob enough STM for the fee to clear the lsfDepositAuth flag.
        env (pay (alice, bob, drops(env.current()->fees().base)));

        // bob clears the lsfDepositAuth and the next payment succeeds.
        env (fclear (bob, asfDepositAuth));
        env.close();

        env (pay (alice, bob, USD (50)));
        env.close();

        env (pay (alice, bob, drops(1)), sendmax (USD (1)));
        env.close();
    }

    void testPayXRP()
    {
        // Exercise direct STM payments to an account that has the
        // lsfDepositAuth flag set.
        testcase ("Pay STM");

        using namespace jtx;
        Account const alice {"alice"};
        Account const bob {"bob"};

        Env env (*this);

        env.fund (STM (10000), alice, bob);

        // bob sets the lsfDepositAuth flag.
        env (fset (bob, asfDepositAuth), fee (drops (10)));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) == STM (10000) - drops(10));

        // bob has more STM than the base reserve.  Any STM payment should fail.
        env (pay (alice, bob, drops(1)), ter (tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) == STM (10000) - drops(10));

        // Change bob's STM balance to exactly the base reserve.
        {
            STAmount const bobPaysXRP {
                env.balance (bob, STM) - reserve (env, 1)};
            XRPAmount const bobPaysFee {reserve (env, 1) - reserve (env, 0)};
            env (pay (bob, alice, bobPaysXRP), fee (bobPaysFee));
            env.close();
        }

        // bob has exactly the base reserve.  A small enough direct STM
        // payment should succeed.
        BEAST_EXPECT (env.balance (bob, STM) == reserve (env, 0));
        env (pay (alice, bob, drops(1)));
        env.close();

        // bob has exactly the base reserve + 1.  No payment should succeed.
        BEAST_EXPECT (env.balance (bob, STM) == reserve (env, 0) + drops(1));
        env (pay (alice, bob, drops(1)), ter (tecNO_PERMISSION));
        env.close();

        // Take bob down to a balance of 0 STM.
        env (noop (bob), fee (reserve (env, 0) + drops(1)));
        env.close ();
        BEAST_EXPECT (env.balance (bob, STM) == drops(0));

        // We should not be able to pay bob more than the base reserve.
        env (pay (alice, bob, reserve (env, 0) + drops(1)),
            ter (tecNO_PERMISSION));
        env.close();

        // However a payment of exactly the base reserve should succeed.
        env (pay (alice, bob, reserve (env, 0) + drops(0)));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) == reserve (env, 0));

        // We should be able to pay bob the base reserve one more time.
        env (pay (alice, bob, reserve (env, 0) + drops(0)));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) ==
            (reserve (env, 0) + reserve (env, 0)));

        // bob's above the threshold again.  Any payment should fail.
        env (pay (alice, bob, drops(1)), ter (tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) ==
            (reserve (env, 0) + reserve (env, 0)));

        // Take bob back down to a zero STM balance.
        env (noop (bob), fee (env.balance (bob, STM)));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) == drops(0));

        // bob should not be able to clear lsfDepositAuth.
        env (fclear (bob, asfDepositAuth), ter (terINSUF_FEE_B));
        env.close();

        // We should be able to pay bob 1 drop now.
        env (pay (alice, bob, drops(1)));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) == drops(1));

        // Pay bob enough so he can afford the fee to clear lsfDepositAuth.
        env (pay (alice, bob, drops(9)));
        env.close();

        // Interestingly, at this point the terINSUF_FEE_B retry grabs the
        // request to clear lsfDepositAuth.  So the balance should be zero
        // and lsfDepositAuth should be cleared.
        BEAST_EXPECT (env.balance (bob, STM) == drops(0));
        env.require (nflags (bob, asfDepositAuth));

        // Since bob no longer has lsfDepositAuth set we should be able to
        // pay him more than the base reserve.
        env (pay (alice, bob, reserve (env, 0) + drops(1)));
        env.close();
        BEAST_EXPECT (env.balance (bob, STM) == reserve (env, 0) + drops(1));
    }

    void testNoRipple()
    {
        // It its current incarnation the DepositAuth flag does not change
        // any behaviors regarding rippling and the NoRipple flag.
        // Demonstrate that.
        testcase ("No Ripple");

        using namespace jtx;
        Account const gw1 ("gw1");
        Account const gw2 ("gw2");
        Account const alice ("alice");
        Account const bob ("bob");

        IOU const USD1 (gw1["USD"]);
        IOU const USD2 (gw2["USD"]);

        auto testIssuer = [&] (FeatureBitset const& features,
            bool noRipplePrev,
            bool noRippleNext,
            bool withDepositAuth)
        {
            assert(!withDepositAuth || features[featureDepositAuth]);

            Env env(*this, features);

            env.fund(STM(10000), gw1, alice, bob);
            env (trust (gw1, alice["USD"](10), noRipplePrev ? tfSetNoRipple : 0));
            env (trust (gw1, bob["USD"](10), noRippleNext ? tfSetNoRipple : 0));
            env.trust(USD1 (10), alice, bob);

            env(pay(gw1, alice, USD1(10)));

            if (withDepositAuth)
                env(fset(gw1, asfDepositAuth));

            auto const result =
                (noRippleNext && noRipplePrev) ? tecPATH_DRY : tesSUCCESS;
            env (pay (alice, bob, USD1(10)), path (gw1), ter (result));
        };

        auto testNonIssuer = [&] (FeatureBitset const& features,
            bool noRipplePrev,
            bool noRippleNext,
            bool withDepositAuth)
        {
            assert(!withDepositAuth || features[featureDepositAuth]);

            Env env(*this, features);

            env.fund(STM(10000), gw1, gw2, alice);
            env (trust (alice, USD1(10), noRipplePrev ? tfSetNoRipple : 0));
            env (trust (alice, USD2(10), noRippleNext ? tfSetNoRipple : 0));
            env(pay(gw2, alice, USD2(10)));

            if (withDepositAuth)
                env(fset(alice, asfDepositAuth));

            auto const result =
                (noRippleNext && noRipplePrev) ? tecPATH_DRY : tesSUCCESS;
            env (pay (gw1, gw2, USD2 (10)),
                path (alice), sendmax (USD1 (10)), ter (result));
        };

        // Test every combo of noRipplePrev, noRippleNext, and withDepositAuth
        for (int i = 0; i < 8; ++i)
        {
            auto const noRipplePrev = i & 0x1;
            auto const noRippleNext = i & 0x2;
            auto const withDepositAuth = i & 0x4;
            testIssuer(
                supported_amendments() | featureDepositAuth,
                noRipplePrev,
                noRippleNext,
                withDepositAuth);

            if (!withDepositAuth)
                testIssuer(
                    supported_amendments() - featureDepositAuth,
                    noRipplePrev,
                    noRippleNext,
                    withDepositAuth);

            testNonIssuer(
                supported_amendments() | featureDepositAuth,
                noRipplePrev,
                noRippleNext,
                withDepositAuth);

            if (!withDepositAuth)
                testNonIssuer(
                    supported_amendments() - featureDepositAuth,
                    noRipplePrev,
                    noRippleNext,
                    withDepositAuth);
        }
    }

    void run() override
    {
        testEnable();
        testPayIOU();
        testPayXRP();
        testNoRipple();
    }
};

BEAST_DEFINE_TESTSUITE(DepositAuth,app,ripple);

} // test
} // ripple
