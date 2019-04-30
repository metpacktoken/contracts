#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include "../metpacktoken/metpacktoken.hpp"

using namespace eosio;

class [[eosio::contract]] mptcrowdsale : public contract {
    public:
        using contract::contract;

        mptcrowdsale(name receiver, name code,  datastream<const char*> ds):contract(receiver, code, ds) {}

        [[eosio::action]]
        void addtoken(  name        token_contract, 
                        name        owner,                        
                        symbol      token_symbol, 
                        uint8_t     rate,
                        uint8_t     ratedenom,
                        uint32_t    crowdsale_start, 
                        uint32_t    crowdsale_end,
                        uint32_t    buyback_start,
                        uint32_t    buyback_end       )
        {
            require_auth(get_self());

        }

        [[eosio::action]]
        void buytokens( name buyer_name, name token_contract, asset eos_sent )
        {
            
        }

        [[eosio::action]]
        void returntokens( name buyer_name, name token_contract, asset amount )
        {

        }

        [[eosio::action]]
        void claim_unlocked_funds( name owner, name token_contract )
        {

        }

        [[eosio::action]]
        void process_transfer( name from_account, name token_contract, asset amount )
        {

        }

    private:      
        struct [[eosio::table]] token {
            name token_contract;
            name owner;            
            symbol token_symbol;
            asset funds_locked;
            asset funds_unlocked;
            uint rate;
            uint ratedenom;
            uint32_t crowdsale_start;
            uint32_t lockup_period;
            uint32_t buyback_period;     

            uint64_t primary_key()const { return token_contract.value; }
        };

        struct [[eosio::table]] buyer {
            name    buyer_name;
            asset   tokens_untouched;                               

            uint64_t primary_key() const { return buyer_name.value; }
        };

        typedef eosio::multi_index<"stats"_n, token > stats;
        typedef eosio::multi_index<"buyers"_n, buyer> buyers;
};

EOSIO_DISPATCH( mptcrowdsale, (addtoken) (buytokens) (returntokens) (claim_unlocked_funds) (process_transfer))