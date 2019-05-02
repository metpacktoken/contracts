#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
// #include "override.hpp"

using namespace eosio;

class [[eosio::contract]] mptcrowdsale : public contract {
    public:
        using contract::contract;

        mptcrowdsale(name receiver, name code,  datastream<const char*> ds):contract(receiver, code, ds) {}

        [[eosio::action]]
        void addtoken(  name        token_contract, 
                        name        owner,                        
                        symbol      token_symbol, 
                        asset       initial_funds,
                        uint8_t     rate,
                        uint8_t     ratedenom,
                        asset       minimum_buy,
                        uint32_t    crowdsale_start, 
                        uint32_t    crowdsale_end,
                        uint32_t    buyback_start,
                        uint32_t    buyback_end       )
        {
            require_auth(get_self());
            
            eosio_assert( token_symbol.is_valid(), "invalid symbol name" );
            stats statstable( get_self(), get_self().value );
            auto existing = statstable.find( token_contract.value );
            eosio_assert( existing == statstable.end(), "token already added" );
            
            statstable.emplace( get_self(), [&]( auto& s ) {
                s.token_contract    = token_contract;
                s.owner             = owner;
                s.token_symbol      = token_symbol;
                s.funds_total       = initial_funds;
                s.funds_unlocked    = initial_funds;
                s.rate              = rate;
                s.ratedenom         = ratedenom;
                s.minimum_buy       = minimum_buy;
                s.crowdsale_start   = crowdsale_start;
                s.crowdsale_end     = crowdsale_end;
                s.buyback_start     = buyback_start;
                s.buyback_end       = buyback_end;          
            });
        }

        [[eosio::action]]
        void returntokens( name buyer_name, name token_contract, asset amount )
        {

        }

        [[eosio::action]]
        void claimfunds( name owner, name token_contract )
        {

        }

        [[eosio::action]]
        void chcktransfer( name from_account, name token_contract, asset amount )
        {

        }

        void transfer(uint64_t sender, uint64_t receiver) 
        {   
            struct transfer_t {
                eosio::name from;
                eosio::name to;
                eosio::asset quantity;
                std::string memo;
            } data = eosio::unpack_action_data<transfer_t>();                     
            if (data.from != get_self()) 
            {                
                this->buytokens(data.from, data.quantity);
            }
        }

        void buytokens( name buyer_name, asset payment )
        {
            // Checks
            // require_auth(buyer_name); // can only buy for own account
            stats statstable( get_self(), get_self().value );            
            token token_entry = statstable.get("metpacktoken"_n.value, "token not found");
            eosio_assert(payment.symbol.raw() == token_entry.funds_total.symbol.raw(), "incorrect payment token");
            eosio_assert(payment.amount >= token_entry.minimum_buy.amount, "payment too small");
            // Check timestamp
            eosio_assert(now() > token_entry.crowdsale_start, "crowdsale has not started");
            eosio_assert(now() < token_entry.crowdsale_end, "crowdsale period is over");

            token_entry.funds_total.amount += payment.amount;

            // calculate tokens to send
            int64_t token_amount = payment.amount * token_entry.rate / token_entry.ratedenom;
            asset tokens_bought(token_amount, token_entry.token_symbol);
            
            // send tokens
            action sendTokens = action( 
                //permission_level
                permission_level(get_self(),"active"_n),
                //code (target contract)
                "metpacktoken"_n,
                //action in target contract
                "transfer"_n,
                //data
                std::make_tuple(get_self(), buyer_name, tokens_bought, std::string("MPT_crowdsale_buy"))
            );

            sendTokens.send();
            // add/update account to/in buyers
            buyers buyerlist(get_self(), get_self().value);
            auto iterator = buyerlist.find(buyer_name.value);
            if (iterator == buyerlist.end() )
            {
                // add buyer to table
                buyerlist.emplace(get_self(), [&]( auto& row ){
                    row.buyer_name = buyer_name;
                    row.tokens_untouched = tokens_bought;
                });
            }
            else
            {
                buyerlist.modify(iterator, get_self(), [&]( auto& row ) {
                    row.tokens_untouched += tokens_bought;
                });
            }

        }

        

    private:      
        struct [[eosio::table]] token {
            name token_contract;
            name owner;            
            symbol token_symbol;
            asset funds_total;
            asset funds_unlocked;
            uint8_t rate;
            uint8_t ratedenom;
            asset minimum_buy;
            uint32_t crowdsale_start;
            uint32_t crowdsale_end;
            uint32_t buyback_start;
            uint32_t buyback_end;                 

            uint64_t primary_key() const { return token_contract.value; }
        };

        struct [[eosio::table]] buyer {
            name    buyer_name;
            asset   tokens_untouched;                               

            uint64_t primary_key() const { return buyer_name.value; }
        };

        typedef eosio::multi_index<"stats"_n, token > stats;
        typedef eosio::multi_index<"buyers"_n, buyer> buyers;
};

extern "C" void apply(uint64_t receiver, uint64_t code, uint64_t action) 
{
    if( action == "transfer"_n.value && code == "eosio.token"_n.value ) 
    {
        execute_action<mptcrowdsale>( name(receiver), name(code),&mptcrowdsale::transfer );
    }
    else if( code == receiver ) 
    {
        switch( action ) {
        EOSIO_DISPATCH_HELPER( mptcrowdsale, (addtoken) (returntokens) (claimfunds) (chcktransfer) );
    }                                       
  }
}




