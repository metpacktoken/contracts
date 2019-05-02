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
        void claimfunds( name owner )
        {
            require_auth(owner);
            stats statstable( get_self(), get_self().value );            
            token token_entry = statstable.get("metpacktoken"_n.value, "token not found");
            eosio_assert(token_entry.funds_unlocked.amount > 0, "No unlocked funds available");
            eosio_assert(token_entry.owner == owner, "only owner can claim funds");
            
            asset claimed_funds = token_entry.funds_unlocked;

            statstable.modify(statstable.begin(), get_self(), [&]( auto& row ) {
                row.funds_total -= claimed_funds;
                row.funds_unlocked -= claimed_funds;
            });

            // send tokens
            action transferEos = action( 
                //permission_level
                permission_level(get_self(),"active"_n),
                //code (target contract)
                "eosio.token"_n,
                //action in target contract
                "transfer"_n,
                //data
                std::make_tuple(get_self(), token_entry.owner, claimed_funds, std::string("claim_unlocked_funds"))
            );

            transferEos.send();
        }

        [[eosio::action]]
        void chcktransfer( name from_account, asset amount, asset total_balance )
        {
            require_auth( "metpacktoken"_n );
            // Check amount of untouched tokens
            buyers buyerlist( get_self(), get_self().value );
            auto iterator = buyerlist.find( from_account.value );            
            if ( iterator == buyerlist.end() )
            {
                // from_account did not buy tokens in crowdsale
                return;
            }
            else
            {
                // check if account has airdrop tokens left to transfer
                const buyer& from = buyerlist.get( from_account.value );
                uint64_t freetokens = total_balance.amount - from.tokens_untouched.amount;                
                // substract remaining from untouched tokens
                uint64_t tokens_to_substract = amount.amount - freetokens;
                if( tokens_to_substract == from.tokens_untouched.amount) 
                {
                    buyerlist.erase( from );
                }
                else
                {
                    buyerlist.modify(iterator, get_self(), [&]( auto& row ) {
                        row.tokens_untouched.amount -= tokens_to_substract;
                    });
                    unlockeos( tokens_to_substract );
                }                
            }
        }
        

        void buytokens( name buyer_name, asset payment )
        {
            // Checks
            require_auth(buyer_name); // can only buy for own account
            stats statstable( get_self(), get_self().value );            
            token token_entry = statstable.get("metpacktoken"_n.value, "token not found");
            eosio_assert(payment.symbol.raw() == token_entry.funds_total.symbol.raw(), "incorrect payment token");
            eosio_assert(payment.amount >= token_entry.minimum_buy.amount, "payment too small");
            // Check timestamp
            eosio_assert(now() > token_entry.crowdsale_start, "crowdsale has not started");
            eosio_assert(now() < token_entry.crowdsale_end, "crowdsale period is over");

            // modify funds in stat table
            statstable.modify(statstable.begin(), get_self(), [&]( auto& row ) {
                row.funds_total += payment;
            });
            

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

        void transfer(uint64_t sender, uint64_t receiver) 
        {   
            struct transfer_t {
                eosio::name from;
                eosio::name to;
                eosio::asset quantity;
                std::string memo;
            } 
            data = eosio::unpack_action_data<transfer_t>();                     
            if (data.from != get_self()) 
            {                
                this->buytokens(data.from, data.quantity);
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

        void unlockeos( uint64_t amount )
        {
            stats statstable( get_self(), get_self().value );
            const auto& st = statstable.get( "metpacktoken"_n.value, "token not found" );
            uint64_t eos_freed = amount * st.ratedenom / st.rate;
            statstable.modify(statstable.begin(), get_self(), [&]( auto& row ){
                row.funds_unlocked.amount += eos_freed;
            });
        }
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




