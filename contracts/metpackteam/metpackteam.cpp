#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

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
            auto existing = statstable.find( curr.code().raw() );
            // eosio_assert( existing == statstable.end(), "token with symbol already initialized" );
            // Modification possible during testing
            if( existing == statstable.end())
            {
                statstable.emplace( get_self(), [&]( auto& s ) {
                    s.tokencontract = tokencontract;
                    s.currency = curr;
                    s.airdropTimestamp = airdropTime;
                    s.lockupPeriodSeconds = lockupPeriod;
                    s.totalCredit = 0;              
                });
            }
            else
            {
                statstable.modify(existing, get_self(), [&]( auto& s ) {
                    s.tokencontract = tokencontract;
                    s.currency = curr;
                    s.airdropTimestamp = airdropTime;
                    s.lockupPeriodSeconds = lockupPeriod;
                    s.totalCredit = 0; 
                });
            }
            
            
        }

        // [[eosio::action]]
        // void withdraw( name member, name tokencontract, symbol currency )
        // {
        //     require_auth( member );
        //     eosio_assert(canwithdraw(tokencontract, currency), "contract not initialized");        
        //     eosio_assert(now() > airdropTimestamp + lockupPeriodSeconds, "tokens are still locked");
        //     team_index teamlist(get_self(), get_self().value);        
        //     asset credit = teamlist.get(member.value, "team member not found").credit;
        //     // substract credit from totalCredit
        //     totalCredit -= credit.amount;
        //     // Transfer MPT to member account
        //     action withdrawCredit = action( 
        //         //permission_level
        //         permission_level(get_self(),"active"_n),
        //         //code (target contract)
        //         "metpacktoken"_n,
        //         //action in target contract
        //         "transfer"_n,
        //         //data
        //         std::make_tuple(get_self(), member, credit, "teamwithdraw")
        //     );

        //     withdrawCredit.send();
        // }

        [[eosio::action]]
        void addmember( name member_name, name contractname, asset member_credit )
        {
            require_auth(_self);
            eosio_assert( member_credit.symbol.is_valid(), "invalid symbol name" );
            stats statstable( get_self(), get_self().value );
            auto statsiterator = statstable.find( contractname.value );
            eosio_assert( statsiterator != statstable.end(), "contract not initialized");
            // TODO: Check if this contract has enough balance to pay out the credit 

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

EOSIO_DISPATCH( metpackteam, (addtoken) (addmember) )
