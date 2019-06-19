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
                        asset       available_tokens,
                        asset       minimum_buy,
                        asset       initial_funds,
                        uint64_t    rate,
                        uint64_t    ratedenom,                        
                        uint32_t    crowdsale_start, 
                        uint32_t    crowdsale_end,
                        uint32_t    buyback_start,
                        uint32_t    buyback_end       )
        {
            require_auth(get_self());
            
            check( available_tokens.symbol.is_valid(), "invalid symbol name" );
            stats statstable( get_self(), get_self().value );
            auto existing = statstable.find( token_contract.value );
            check( existing == statstable.end(), "token already added" );
            
            statstable.emplace( get_self(), [&]( auto& s ) {
                s.token_contract    = token_contract;
                s.owner             = owner;              
                s.available_tokens  = available_tokens;
                s.minimum_buy       = minimum_buy;
                s.funds_total       = initial_funds;
                s.funds_unlocked    = initial_funds;
                s.rate              = rate;
                s.ratedenom         = ratedenom;                
                s.crowdsale_start   = crowdsale_start;
                s.crowdsale_end     = crowdsale_end;
                s.buyback_start     = buyback_start;
                s.buyback_end       = buyback_end;          
            });
        }

        [[eosio::action]]
        void claimfunds( name token_contract )
        {
            require_auth(token_contract);
            stats statstable( get_self(), get_self().value );            
            const auto& token_entry = statstable.get( token_contract.value, "token not found");
            check(token_entry.funds_unlocked.amount > 0, "No unlocked funds available");
            check(token_entry.owner == token_contract, "only token contract can claim funds");
            
            asset claimed_funds = token_entry.funds_unlocked;

            statstable.modify( token_entry, get_self(), [&]( auto& row ) {
                row.funds_total -= claimed_funds;
                row.funds_unlocked -= claimed_funds;
            });

            // send EOS
            action transfer_eos = action( 
                //permission_level
                permission_level(get_self(), name("active")),
                //code (target contract)
                name("eosio.token"),
                //action in target contract
                name("transfer"),
                //data
                std::make_tuple(get_self(), token_entry.owner, claimed_funds, std::string("claim_unlocked_funds"))
            );

            transfer_eos.send();
        }

        [[eosio::action]]
        void chcktransfer( name from_account, asset amount, asset total_balance )
        {
            require_auth( name("metpacktoken") );
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
                const auto& from = buyerlist.get( from_account.value );
                uint64_t freetokens = total_balance.amount - from.tokens_untouched.amount;
                if (freetokens >= amount.amount) return; // Airdrop amount covers transaction
                // substract remaining from untouched tokens
                uint64_t tokens_to_substract = amount.amount - freetokens;
                if( tokens_to_substract == from.tokens_untouched.amount) 
                {
                    buyerlist.erase( from );
                }
                else
                {
                    buyerlist.modify( from , get_self(), [&]( auto& row ) {
                        row.tokens_untouched.amount -= tokens_to_substract;
                    });
                    unlockeos( tokens_to_substract );
                }                
            }
        }    

        void transfer(uint64_t sender, uint64_t receiver) 
        {   
            struct transfer_t {
                name from;
                name to;
                asset quantity;
                std::string memo;
            } 
            data = unpack_action_data<transfer_t>();                     
            if (data.from != get_self() && data.to == get_self()) 
            {                
                buytokens(data.from, data.quantity);
            }
        }

        void processreturn(uint64_t sender, uint64_t receiver) 
        {   
            struct transfer_t {
                name from;
                name to;
                asset quantity;
                std::string memo;
            } 
            data = unpack_action_data<transfer_t>();                     
            if (data.from != get_self() && data.from != name("metpacktoken") && data.to == get_self()) 
            {                
                returntokens(data.from, data.quantity);
            }
        }

        

    private:      
        struct [[eosio::table]] token {
            name     token_contract;
            name     owner;
            asset    available_tokens;
            asset    minimum_buy;
            asset    funds_total;
            asset    funds_unlocked;
            uint64_t rate;
            uint64_t ratedenom;            
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

        typedef eosio::multi_index< name("stats"), token > stats;
        typedef eosio::multi_index< name("buyers"), buyer> buyers;

        void unlockeos( uint64_t amount )
        {
            stats statstable( get_self(), get_self().value );
            const auto& st = statstable.get( name("metpacktoken").value, "token not found" );
            uint64_t eos_freed = amount * st.ratedenom / st.rate;
            statstable.modify(statstable.begin(), get_self(), [&]( auto& row ){
                row.funds_unlocked.amount += eos_freed;
            });
        }

        void buytokens( name buyer_name, asset payment )
        {
            // Checks
            require_auth(buyer_name); // can only buy for own account
            stats statstable( get_self(), get_self().value );            
            const auto& token_entry = statstable.get( name("metpacktoken").value, "token not found");
            check(payment.symbol.raw() == token_entry.funds_total.symbol.raw(), "incorrect payment token");
            check(payment.amount >= token_entry.minimum_buy.amount, "payment too small");
            // check available funds
            // Check timestamp
            check(now() > token_entry.crowdsale_start, "crowdsale has not started");
            check(now() < token_entry.crowdsale_end, "crowdsale period is over");
            // calculate tokens to send and check available amount
            int64_t token_amount = payment.amount * token_entry.rate / token_entry.ratedenom;
            asset tokens_bought(token_amount, token_entry.available_tokens.symbol);
            check(tokens_bought <= token_entry.available_tokens, "not enough tokens available");

            // modify funds in stat table
            statstable.modify( token_entry, get_self(), [&]( auto& row ) {
                row.funds_total += payment;
                row.available_tokens -= tokens_bought;
            });
            
            // send tokens
            action sendTokens = action( 
                //permission_level
                permission_level(get_self(), name("active")),
                //code (target contract)
                name("metpacktoken"),
                //action in target contract
                name("transfer"),
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

        void returntokens( name from_account, asset amount )
        {
            stats statstable( get_self(), get_self().value );
            const auto& st = statstable.get( name("metpacktoken").value, "token not found" );
            // check timestamps
            check(now() > st.buyback_start, "buyback period has not started yet");
            check(now() < st.buyback_end, "buyback period is over");
            // check amount
            check(amount.symbol == st.available_tokens.symbol, "wrong token symbol");
            buyers buyerlist( get_self(), get_self().value );            
            const auto& from = buyerlist.get( from_account.value, "only untraded crowdsale tokens are accepted");
            check(amount <= from.tokens_untouched, "not enough valid tokens");
            // edit untouched tokens
            if (amount == from.tokens_untouched) buyerlist.erase( from );
            else
            {
                buyerlist.modify( from, get_self(), [&]( auto& row ) {
                    row.tokens_untouched -= amount;
                });
            }
            // calculate amount of EOS to return and edit funds
            asset eos_to_return(amount.amount * st.ratedenom / st.rate, st.funds_total.symbol);
            statstable.modify( st, get_self(), [&]( auto& s ){
                s.funds_total -= eos_to_return;
            });
            // send EOS to from_account
            action transfer_eos = action( 
                //permission_level
                permission_level(get_self(), name("active")),
                //code (target contract)
                name("eosio.token"),
                //action in target contract
                name("transfer"),
                //data
                std::make_tuple(get_self(), from_account, eos_to_return, std::string("mpt_buyback"))
            );
            transfer_eos.send();
        }
};

extern "C" void apply(uint64_t receiver, uint64_t code, uint64_t action) 
{
    if( action == name("transfer").value && code == name("eosio.token").value ) 
    {
        execute_action<mptcrowdsale>( name(receiver), name(code),&mptcrowdsale::transfer );
    }
    else if ( action == name("transfer").value && code == name("metpacktoken").value)
    {
        execute_action<mptcrowdsale>( name(receiver), name(code),&mptcrowdsale::processreturn );
    }
    else if( code == receiver ) 
    {
        switch( action ) {
        EOSIO_DISPATCH_HELPER( mptcrowdsale, (addtoken) (chcktransfer) (claimfunds) );
    }                                       
  }
}




