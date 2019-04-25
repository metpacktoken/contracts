#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include "../metpacktoken/metpacktoken.hpp"

using namespace eosio;

class [[eosio::contract]] metpackteam : public contract {
    public:
        using contract::contract;

        metpackteam(name receiver, name code,  datastream<const char*> ds):contract(receiver, code, ds) {}

        [[eosio::action]]
        void addtoken( name tokencontract, symbol curr, uint32_t airdropTime, uint32_t lockupPeriod )
        {
            require_auth(get_self());
            // Lookup currency in statstable
            eosio_assert( curr.is_valid(), "invalid symbol name" );
            stats statstable( get_self(), get_self().value );
            auto existing = statstable.find( tokencontract.value );
            eosio_assert( existing == statstable.end(), "token with symbol already exists" );
            // Modification possible during testing
            statstable.emplace( get_self(), [&]( auto& s ) {
                s.tokencontract = tokencontract;
                s.currency = curr;
                s.airdropTimestamp = airdropTime;
                s.lockupPeriodSeconds = lockupPeriod;
                s.totalCredit = 0;
            });
        }

        [[eosio::action]]
        void addmember( name member_name, name contractname, asset member_credit )
        {
            require_auth(_self);
            eosio_assert( member_credit.symbol.is_valid(), "invalid symbol name" );
            stats statstable( get_self(), get_self().value );
            auto statsiterator = statstable.find( contractname.value );
            eosio_assert( statsiterator != statstable.end(), "contract not initialized");
            // Check if this contract has enough balance to pay out the credit 
            asset currbalance = token::get_balance( contractname, get_self(), member_credit.symbol.code());
            eosio_assert( currbalance.amount >= statstable.get(contractname.value).totalCredit + member_credit.amount, "insufficient funds");

            // update total owed credit
            statstable.modify(statsiterator, get_self(), [&]( auto& statrow )
            {
                statrow.totalCredit += member_credit.amount;
            });         
            team_index teamlist(get_self(), get_self().value);
            auto teamiterator = teamlist.find(member_name.value);
            eosio_assert(teamiterator == teamlist.end(), "team member already in table" );            
            teamlist.emplace(get_self(), [&]( auto& row ) 
            {   
                row.accountname = member_name;
                row.credit = member_credit;
            });
        }

        [[eosio::action]]
        void withdraw( name owner, name tokencontract, symbol currency )
        {
            require_auth( owner );
            eosio_assert(currency.is_valid(), "invalid symbol");

            stats statstable( get_self(), get_self().value );
            
            stat token_entry = statstable.get(tokencontract.value, "token not found");
            eosio_assert( token_entry.currency == currency, "invalid token" );
            team_index teamlist(get_self(), get_self().value);
            auto iterator = teamlist.find( owner.value );
            member team_member = teamlist.get(owner.value, "team member not found");
            
            eosio_assert(token_entry.airdropTimestamp + token_entry.lockupPeriodSeconds < now(), "lockup period has not passed, be patient!");

            asset to_pay = team_member.credit;

            // remove team member from table
            teamlist.erase(iterator);

            // Transfer MPT to member account
            action withdrawCredit = action( 
                //permission_level
                permission_level(get_self(),"active"_n),
                //code (target contract)
                "metpacktoken"_n,
                //action in target contract
                "transfer"_n,
                //data
                std::make_tuple(get_self(), owner, to_pay, std::string("teamwithdraw"))
            );

            withdrawCredit.send();
        }

    private:      
        struct [[eosio::table]] stat {
            name        tokencontract;
            symbol      currency;
            uint32_t    airdropTimestamp;
            uint32_t    lockupPeriodSeconds;
            uint64_t    totalCredit;         

            uint64_t primary_key()const { return tokencontract.value; }
        };

        struct [[eosio::table]] member {
            name    accountname;
            asset   credit;                       

            uint64_t primary_key() const { return accountname.value; }
        };

        typedef eosio::multi_index<"stats"_n, stat > stats;
        typedef eosio::multi_index<"team"_n, member> team_index;
};

EOSIO_DISPATCH( metpackteam, (addtoken) (addmember) (withdraw))
