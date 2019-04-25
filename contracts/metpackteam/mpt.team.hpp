// #include <eosiolib/asset.hpp>
// #include <eosiolib/eosio.hpp>

// using namespace eosio;

// class [[eosio::contract("mpt.team")]] mptteam : public contract {
//     public:
//         using contract::contract;

//         mptteam(name receiver, name code,  datastream<const char*> ds):contract(receiver, code, ds) {}

//         [[eosio::action]]
//         void init(  uint32_t airdropTime,
//                     uint32_t lockupPeriod );

//         [[eosio::action]]
//         void withdraw( name member );

//         [[eosio::action]]
//         void addmember( name member_name, asset member_credit );

//         // FOR TESTING PURPOSES
//         [[eosio::action]]
//         void changeLockup( uint32_t newLockup);

//     private:
//         bool isInitialized;
//         uint32_t airdropTimestamp;
//         uint32_t lockupPeriodSeconds;
//         uint64_t totalCredit;

//         struct [[eosio::table]] member {
//             name    accountname;
//             asset   credit;                       

//             uint64_t primary_key() const { return accountname.value; }
//         };

//         typedef eosio::multi_index<"team"_n, member> team_index;


// };