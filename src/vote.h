    
#ifndef _LBTC_VOTE_H_
#define _LBTC_VOTE_H_

#include "miner.h"
#include "pubkey.h"
#include <unordered_map>
#include <map>
#include <set>
#include <string>
#include <boost/thread/shared_mutex.hpp>
#include <boost/filesystem.hpp>

struct key_hash
{
    std::size_t operator()(CKeyID const& k) const {
        std::size_t hash = 0;
        boost::hash_range( hash, k.begin(), k.end() );
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

    uint64_t GetAddressBalance(const CKeyID& id);
    void UpdateAddressBalance(const std::vector<std::pair<CKeyID, int64_t>>& addressBalances);

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

    static const int MaxNumberOfVotes = 51;

private:
    uint64_t _GetAddressBalance(const CKeyID& address);
    uint64_t _GetDelegateVotes(const CKeyID& delegate);
    uint64_t _UpdateAddressBalance(const CKeyID& id, int64_t value);

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

private:
    boost::shared_mutex lockVote;

    std::map<CKeyID, std::set<CKeyID>> mapDelegateVoters;
    std::map<CKeyID, std::set<CKeyID>> mapVoterDelegates;
    std::map<CKeyID, std::string> mapDelegateName;
    std::map<std::string, CKeyID> mapNameDelegate;
    std::map<uint256, uint64_t>  mapHashHeightInvalidVote;
    boost::shared_mutex lockMapHashHeightInvalidVote;

    //std::unordered_map<CKeyID, uint64_t, key_hash> mapAddressBalance;
    std::unordered_map<CKeyID, int64_t, key_hash> mapAddressBalance;

    std::string strFilePath;
    std::string strDelegateFileName;
    std::string strVoteFileName;
    std::string strBalanceFileName;
    std::string strControlFileName;
    std::string strInvalidVoteTxFileName;
    std::string strOldBlockHash;
    int64_t nOldBlockHeight;
};

#endif
