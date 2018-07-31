    
#ifndef _LBTC_VOTE_H_
#define _LBTC_VOTE_H_

#include "miner.h"
#include "pubkey.h"
#include <unordered_map>
#include <map>
#include <set>
#include <boost/thread/shared_mutex.hpp>
#include <boost/filesystem.hpp>

typedef std::pair<uint160, uint8_t> CMyAddress;

struct key_hash
{
    std::size_t operator()(CMyAddress const& k) const {
        std::size_t hash = 0;
        boost::hash_range( hash, k.first.begin(), k.first.end() );
        return hash;
    }
};

class Vote{
public:
    Vote();
    ~Vote();
    bool Init(int64_t nBlockHeight, const std::string& strBlockHash);
    static Vote& GetInstance();
    std::vector<Delegate> GetTopDelegateInfo(uint64_t nMinHoldBalance, uint32_t nDelegateNum);

    bool ProcessVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height, bool fUndo);
    bool ProcessCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height, bool fUndo);
    bool ProcessRegister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height, bool fUndo);

    uint64_t GetDelegateVotes(const CKeyID& delegate);
    std::set<CKeyID> GetDelegateVoters(CKeyID& delegate);
    std::set<CKeyID> GetVotedDelegates(CKeyID& delegate);
    std::map<std::string, CKeyID> ListDelegates();

    bool Store(int64_t height, const std::string& strBlockHash);
    bool Load(int64_t height, const std::string& strBlockHash);

    uint64_t GetAddressBalance(const CMyAddress& id);
    void UpdateAddressBalance(const std::vector<std::pair<CMyAddress, int64_t>>& addressBalances);

    CKeyID GetDelegate(const std::string& name);
    std::string GetDelegate(CKeyID keyid);
    bool HaveDelegate(const std::string& name, CKeyID keyid);
    bool HaveDelegate_Unlock(const std::string& name, CKeyID keyid);
    bool HaveVote(CKeyID voter, CKeyID delegate);

    bool HaveDelegate(std::string name);
    bool HaveDelegate(CKeyID keyID);
    int64_t GetOldBlockHeight() {return nOldBlockHeight;}
    std::string GetOldBlockHash() {return strOldBlockHash;}

    void AddInvalidVote(uint256 hash, uint64_t height);
    void DeleteInvalidVote(uint64_t height);
    bool FindInvalidVote(uint256 hash);

    bool AddDelegateMultiaddress(const CMyAddress& delegate, const CMyAddress& multiAddress, const uint256& txid);
    bool DelDelegateMultiaddress(const CMyAddress& delegate, const CMyAddress& multiAddress, const uint256& txid);
    std::map<CMyAddress, uint256> GetDelegateMultiaddress(const CMyAddress& delegate);


    static const int MaxNumberOfVotes = 51;
    uint64_t GetDelegateFunds(const CMyAddress& address);

private:
    uint64_t _GetAddressBalance(const CMyAddress& address);
    uint64_t _UpdateAddressBalance(const CMyAddress& id, int64_t value);
    uint64_t _GetDelegateVotes(const CKeyID& delegate);

    bool RepairFile(int64_t nBlockHeight, const std::string& strBlockHash);
    bool ReadControlFile(int64_t& nBlockHeight, std::string& strBlockHash, const std::string& strFileName);
    bool WriteControlFile(int64_t nBlockHeight, const std::string& strBlockHash, const std::string& strFileName);

    bool Read();
    bool Write(const std::string& strBlockHash);
    void Delete(const std::string& strBlockHash);

    bool ProcessVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessRegister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height);
    bool ProcessUnregister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height);

    bool ProcessUndoVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessUndoCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height);
    bool ProcessUndoRegister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height);

    bool ProcessVote(CKeyID& voter, const std::set<CKeyID>& delegates);
    bool ProcessCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates);
    bool ProcessRegister(CKeyID& delegate, const std::string& strDelegateName);
    bool ProcessUnregister(CKeyID& delegate, const std::string& strDelegateName);

    std::map<CMyAddress, std::map<CMyAddress, uint256>>& GetDelegateMultiaddress() {return mapDelegateMultiaddress;}
    bool IsValidDelegate(const CMyAddress& address, uint64_t nMinHoldBalance);


private:
    int64_t nVersion;
    const int64_t nCurrentVersion = -1;
    boost::shared_mutex lockVote;

    std::map<CKeyID, std::set<CKeyID>> mapDelegateVoters;
    std::map<CKeyID, std::set<CKeyID>> mapVoterDelegates;
    std::map<CKeyID, std::string> mapDelegateName;
    std::map<std::string, CKeyID> mapNameDelegate;
    std::map<uint256, uint64_t>  mapHashHeightInvalidVote;
    boost::shared_mutex lockMapHashHeightInvalidVote;

    std::unordered_map<CMyAddress, uint64_t, key_hash> mapAddressBalance;

    std::map<CMyAddress, std::map<CMyAddress, uint256>> mapDelegateMultiaddress;

    std::string strFilePath;
    std::string strDelegateFileName;
    std::string strVoteFileName;
    std::string strBalanceFileName;
    std::string strControlFileName;
    std::string strInvalidVoteTxFileName;
    std::string strDelegateMultiaddressName;

    std::string strOldBlockHash;
    int64_t nOldBlockHeight;
};

#endif
