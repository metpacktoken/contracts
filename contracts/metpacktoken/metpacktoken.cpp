/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */

#include "metpacktoken.hpp"

namespace eosio {

void token::create( name   issuer,
                    asset  maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}

void token::update( name issuer,
                    const symbol& symbol )
{
    require_auth( _self );

    auto sym_code_raw = symbol.code().raw();

    stats statstable( _self, sym_code_raw );
    auto existing = statstable.find( sym_code_raw );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before update" );
    const auto& st = *existing;
    eosio_assert( st.supply.symbol == symbol, "symbol precision mismatch" );


    statstable.modify( st, same_payer, [&]( auto& s ) {
      s.max_supply    = s.max_supply;   
      s.issuer        = issuer;
    });
}

void token::issue( name to, asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer, true);

    if( to != st.issuer ) {
      SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} },
                          { st.issuer, to, quantity, memo }
      );
    }
}

void token::retire( asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must retire positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply -= quantity;
    });

    sub_balance( st.issuer, quantity );
}

void token::transfer( name    from,
                      name    to,
                      asset   quantity,
                      string  memo )
{
    eosio_assert( from != to, "cannot transfer to self" );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    // don't check transfers from crowdsale contract
    if( from != "mptcrowdsale"_n) checktransfer( from, quantity );
    do_claim( from, quantity.symbol, from );
    sub_balance( from, quantity );
    add_balance( to, quantity, from, from != st.issuer );

    //account needs to exist first, dont auto claim when issuer
    if(from != st.issuer) {
      do_claim( to, quantity.symbol, from );
    }
}

void token::claim( name owner, const symbol& sym ) {
  do_claim(owner,sym,owner);
}

void token::do_claim( name owner, const symbol& sym, name payer ) {
  eosio_assert( sym.is_valid(), "invalid symbol name" );
  auto sym_code_raw = sym.code().raw();

  require_auth( payer );
  accounts owner_acnts( _self, owner.value );

  const auto& existing = owner_acnts.get( sym_code_raw, "no balance object found" );
  if( !existing.claimed ) {
    //save the balance
    auto value = existing.balance;
    //erase the table freeing ram to the issuer
    owner_acnts.erase( existing );
    //create a new index
    auto replace = owner_acnts.find( sym_code_raw );
    //confirm there are definitely no balance now
    eosio_assert(replace == owner_acnts.end(), "there must be no balance object" );
    //add the new claimed balance paid by owner
    owner_acnts.emplace( payer, [&]( auto& a ){
      a.balance = value;
      a.claimed = true;
    });
  }
}

void token::recover( name owner, const symbol& sym ) {
  eosio_assert( sym.is_valid(), "invalid symbol name" );
  auto sym_name = sym;

  stats statstable( _self, sym_name.code().raw());
  auto existing = statstable.find( sym_name.code().raw() );
  eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
  const auto& st = *existing;

  require_auth( st.issuer );

  //fail gracefully so we dont have to take another snapshot
  accounts owner_acnts( _self, owner.value );
  auto owned = owner_acnts.find( sym_name.code().raw() );
  if( owned != owner_acnts.end() ) {
    if( !owned->claimed ) {
      sub_balance( owner, owned->balance );
      add_balance( st.issuer, owned->balance, st.issuer, true );
    }
  }
}

void token::sub_balance( name owner, asset value ) {
   accounts from_acnts( _self, owner.value );

   const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );

   if( from.balance.amount == value.amount ) {
      from_acnts.erase( from );
   } else {
      from_acnts.modify( from, owner, [&]( auto& a ) {
          a.balance -= value;
      });
   }
}

void token::add_balance( name owner, asset value, name ram_payer, bool claimed )
{
   accounts to_acnts( _self, owner.value );
   auto to = to_acnts.find( value.symbol.code().raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
        a.claimed = claimed;
      });
   } else {
      to_acnts.modify( to, same_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void token::open( name owner, const symbol& symbol, name ram_payer )
{
   require_auth( ram_payer );

   auto sym_code_raw = symbol.code().raw();

   stats statstable( _self, sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   eosio_assert( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( _self, owner.value );
   auto it = acnts.find( sym_code_raw );
   if( it == acnts.end() ) {
      acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = asset{0, symbol};
      });
   }
}

void token::close( name owner, const symbol& symbol )
{
   require_auth( owner );
   accounts acnts( _self, owner.value );
   auto it = acnts.find( symbol.code().raw() );
   eosio_assert( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
   eosio_assert( it->balance.amount == 0, "Cannot close because the balance is not zero." );
   acnts.erase( it );
}

void token::checktransfer( name from, asset value)
{
  // send tokens
  action checkTransfer = action( 
      //permission_level
      permission_level(get_self(),"active"_n),
      //code (target contract)
      "mptcrowdsale"_n,
      //action in target contract
      "chcktransfer"_n,
      //data
      std::make_tuple(from, value, get_balance(get_self(), from, value.symbol.code()))
  );

  checkTransfer.send();
}

} /// namespace eosio

EOSIO_DISPATCH( eosio::token, (create)(issue)(transfer)(open)(close)(retire)(claim)(recover)(update) )
