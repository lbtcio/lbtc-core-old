
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <stdlib.h>
#include "util.h"
#include "base58.h"
#include "vote.h"
#include "myserialize.h"

typedef boost::shared_lock<boost::shared_mutex> read_lock;
typedef boost::unique_lock<boost::shared_mutex> write_lock;

using namespace std;

Vote::Vote()
    : mapAddressBalance(100000000)
{
}

Vote::~Vote()
{
}

#define VOTE_FILE "vote.dat"
#define DELEGATE_FILE "delegate.dat"
#define BALANCE_FILE "balance.dat"
#define CONTROL_FILE "control.dat"
#define INVALIDVOTETX_FILE "invalidvotetx.dat"
#define DELEGATE_MULTIADDRESS_FILE "delegatemultiaddress.dat"

#define BILL_FILE "bill.data"
#define COMMITTEE_FILE "committee.data"

bool Vote::Init(int64_t nBlockHeight, const std::string& strBlockHash)
{ 
    //write_lock w(lockVote);
    if(Params().NetworkIDString() == "main") {
        pbill = make_shared<CVoteDBK2<uint160, CSubmitBillData, CKeyID>>(5854000, 300000 * COIN, GetBalance);
        pcommittee = make_shared<CVoteDBK1<CKeyID, CRegisterCommitteeData, CKeyID>>(5854000);
    } else {
        pbill = make_shared<CVoteDBK2<uint160, CSubmitBillData, CKeyID>>(806300, 100 * COIN, GetBalance);
        pcommittee = make_shared<CVoteDBK1<CKeyID, CRegisterCommitteeData, CKeyID>>(806300);
    }

    strFilePath = (GetDataDir() / "dpos").string();
    strDelegateFileName = strFilePath + "/" + DELEGATE_FILE;
    strVoteFileName = strFilePath + "/" + VOTE_FILE;
    strBalanceFileName = strFilePath + "/" + BALANCE_FILE;
    strControlFileName = strFilePath + "/" + CONTROL_FILE;
    strInvalidVoteTxFileName = strFilePath + "/" + INVALIDVOTETX_FILE;
    strDelegateMultiaddressName = strFilePath + "/" + DELEGATE_MULTIADDRESS_FILE;

    strBillFileName = strFilePath + "/" + BILL_FILE;
    strCommitteeFileName = strFilePath + "/" + COMMITTEE_FILE;

    if(nBlockHeight == 0) {
        if(!boost::filesystem::is_directory(strFilePath)) {
            boost::filesystem::create_directories(strFilePath);
        }
        return true;
    } else {
        return Load(nBlockHeight, strBlockHash);
    }
}

bool Vote::ReadControlFile(int64_t& nBlockHeight, std::string& strBlockHash, const std::string& strFileName)
{
    bool ret = false;
    FILE *file = fopen(strFileName.c_str(), "r");
    if(file) {
        fscanf(file, "%ld\n", &nBlockHeight);

        if(nBlockHeight < 0) {
            nVersion = nBlockHeight;
            fscanf(file, "%ld\n", &nBlockHeight);
        }

        char buff[128];
        fscanf(file, "%s\n", buff);
        strBlockHash = buff;

        fclose(file);
        ret = true;
    }

    return ret;
}

bool Vote::WriteControlFile(int64_t nBlockHeight, const std::string& strBlockHash, const std::string& strFileName)
{
    bool ret = false;
    FILE *file = fopen(strFileName.c_str(), "w");
    if(file) {
        fprintf(file, "%ld\n", nCurrentVersion);
        fprintf(file, "%ld\n", nBlockHeight);
        fprintf(file, "%s\n", strBlockHash.c_str());
        fclose(file);
        ret = true;
    }

    return ret;
}

bool Vote::RepairFile(int64_t nBlockHeight, const std::string& strBlockHash)
{
    if(!boost::filesystem::is_directory(strFilePath)) {
        boost::filesystem::create_directories(strFilePath);
        if(boost::filesystem::exists(GetDataDir() / BALANCE_FILE)) {
            boost::filesystem::copy_file(GetDataDir() / BALANCE_FILE, strBalanceFileName + "-" + strBlockHash);
        }

        if(boost::filesystem::exists(GetDataDir() / VOTE_FILE)) {
            boost::filesystem::copy_file(GetDataDir() / VOTE_FILE, strVoteFileName + "-" + strBlockHash);
        }

        if(boost::filesystem::exists(GetDataDir() / DELEGATE_FILE)) {
            boost::filesystem::copy_file(GetDataDir() / DELEGATE_FILE, strDelegateFileName + "-" + strBlockHash);
        }

        return WriteControlFile(nBlockHeight, strBlockHash, strControlFileName);
    }

    int64_t nBlockHeightTemp = 0;
    std::string strBlockHashTemp;
    std::string strFileName;

    strFileName = strControlFileName + "-temp";
    if(boost::filesystem::exists(strFileName)) {
        if(ReadControlFile(nBlockHeightTemp, strBlockHashTemp, strFileName)) {
            if(boost::filesystem::exists(strVoteFileName + "-" + strBlockHashTemp)
                && boost::filesystem::exists(strDelegateFileName + "-" + strBlockHashTemp)
                && boost::filesystem::exists(strBalanceFileName + "-" + strBlockHashTemp))
            {
                rename(strFileName.c_str(), strControlFileName.c_str());
                return true;
            }
            remove(strFileName.c_str());
        }
    }

    strFileName = strControlFileName + "-old";
    if(boost::filesystem::exists(strFileName)) {
        if(ReadControlFile(nBlockHeightTemp, strBlockHashTemp, strFileName)) {
            if(boost::filesystem::exists(strVoteFileName + "-" + strBlockHashTemp)
                && boost::filesystem::exists(strDelegateFileName + "-" + strBlockHashTemp)
                && boost::filesystem::exists(strBalanceFileName + "-" + strBlockHashTemp))
            {
                rename(strFileName.c_str(), strControlFileName.c_str());
                return true;
            }
            remove(strFileName.c_str());
        }
    }

    strFileName = strControlFileName;
    if(boost::filesystem::exists(strFileName)) {
        if(ReadControlFile(nBlockHeightTemp, strBlockHashTemp, strFileName)) {
            if(boost::filesystem::exists(strVoteFileName + "-" + strBlockHashTemp)
                || boost::filesystem::exists(strDelegateFileName + "-" + strBlockHashTemp)
                || boost::filesystem::exists(strBalanceFileName + "-" + strBlockHashTemp))
            {
                return true;
            } else {
                return false;    
            }
        }
    }

    return false;
}

static Vote vote;
Vote& Vote::GetInstance()
{
    return vote;
}

bool Vote::ProcessVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height, bool fUndo)
{
    if(fUndo) {
        return ProcessUndoVote(voter, delegates, hash, height);
    } else {
        return ProcessVote(voter, delegates, hash, height);
    }
}

bool Vote::ProcessVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height)
{
    bool ret = ProcessVote(voter, delegates);
    if(ret == false) {
        AddInvalidVote(hash, height);
        LogPrintf("ProcessVote InvalidTx Hash:%s height:%lld\n", hash.ToString().c_str(), height);
    }

    LogPrintf("DPoS ProcessVote Height:%u\n", height);
    for(auto i : delegates) {
        LogPrintf("DPoS ProcessVote: %s ---> %s(%s)\n", CBitcoinAddress(voter).ToString().c_str(), Vote::GetInstance().GetDelegate(i).c_str(), CBitcoinAddress(i).ToString().c_str());
    }
    if(ret) {
        LogPrintf("DPoS ProcessVote success\n");
    } else {
        LogPrintf("DPoS ProcessVote fail\n");
    }

    return ret;
}

bool Vote::ProcessUndoVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height)
{
    if(FindInvalidVote(hash)) {
        LogPrintf("ProcessUndoVote InvalidTx Hash:%s height:%lld\n", hash.ToString().c_str(), height);
        return true;
    }

    bool ret = ProcessCancelVote(voter, delegates);

    LogPrintf("DPoS ProcessUndoVote Height:%u\n", height);
    for(auto i : delegates) {
        LogPrintf("DPoS ProcessUndoVote: %s ---> %s(%s)\n", CBitcoinAddress(voter).ToString().c_str(), Vote::GetInstance().GetDelegate(i).c_str(), CBitcoinAddress(i).ToString().c_str());
    }
    if(ret) {
        LogPrintf("DPoS ProcessUndoVote success\n");
    } else {
        LogPrintf("DPoS ProcessUndoVote fail\n");
    }

    return ret;
}

bool Vote::ProcessVote(CKeyID& voter, const std::set<CKeyID>& delegates)
{
    write_lock w(lockVote);
    uint64_t votes = 0;

    {
        auto it = mapVoterDelegates.find(voter);
        if(it != mapVoterDelegates.end()) {
            votes = it->second.size();    
        }
    }

    if((votes + delegates.size()) > Vote::MaxNumberOfVotes) {
        return false;
    }

    for(auto item : delegates)
    {
        if(mapDelegateName.find(item) == mapDelegateName.end())
            return false;

        auto it = mapDelegateVoters.find(item);
        if(it != mapDelegateVoters.end()) {
            if(it->second.find(voter) != it->second.end()) {
                return false;    
            }
        }
    }

    for(auto item : delegates)
    {
        mapDelegateVoters[item].insert(voter);
    }

    auto& setVotedDelegates = mapVoterDelegates[voter];
    for(auto item : delegates)
    {
        setVotedDelegates.insert(item);
    }

    return true;
}

bool Vote::ProcessCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height, bool fUndo)
{
    if(fUndo) {
        return ProcessUndoCancelVote(voter, delegates, hash, height);
    } else {
        return ProcessCancelVote(voter, delegates, hash, height);
    }
}

bool Vote::ProcessCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height)
{
    bool ret = ProcessCancelVote(voter, delegates);
    if(ret == false) {
        AddInvalidVote(hash, height);
        LogPrintf("ProcessCancelVote InvalidTx Hash:%s height:%lld\n", hash.ToString().c_str(), height);
    }

    LogPrintf("DPoS ProcessCancelVote Height:%u\n", height);
    for(auto i : delegates) {
        LogPrintf("DPoS ProcessCancelVote: %s ---> %s(%s)\n", CBitcoinAddress(voter).ToString().c_str(), Vote::GetInstance().GetDelegate(i).c_str(), CBitcoinAddress(i).ToString().c_str());
    }
    if(ret) {
        LogPrintf("DPoS ProcessCancelVote success\n");
    } else {
        LogPrintf("DPoS ProcessCancelVote fail\n");
    }

    return ret;
}

bool Vote::ProcessUndoCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates, uint256 hash, uint64_t height)
{
    if(FindInvalidVote(hash)) {
        LogPrintf("ProcessUndoCancelVote InvalidTx Hash:%s height:%lld\n", hash.ToString().c_str(), height);
        return true;
    }

    bool ret = ProcessVote(voter, delegates);
    LogPrintf("DPoS ProcessUndoCancelVote Height:%u\n", height);
    for(auto i : delegates) {
        LogPrintf("DPoS ProcessUndoCancelVote: %s ---> %s(%s)\n", CBitcoinAddress(voter).ToString().c_str(), Vote::GetInstance().GetDelegate(i).c_str(), CBitcoinAddress(i).ToString().c_str());
    }
    if(ret) {
        LogPrintf("DPoS ProcessUndoCancelVote success\n");
    } else {
        LogPrintf("DPoS ProcessUndoCancelVote fail\n");
    }

    return ret;
}

bool Vote::ProcessCancelVote(CKeyID& voter, const std::set<CKeyID>& delegates)
{
    write_lock w(lockVote);
    if(delegates.size() > Vote::MaxNumberOfVotes) {
        return false;
    }

    for(auto item : delegates)
    {
        auto it = mapDelegateVoters.find(item);
        if(it != mapDelegateVoters.end()) {
            if(it->second.find(voter) == it->second.end()) {
                return false;    
            }
        } else {
            return false;
        }
    }

    for(auto item : delegates)
    {
        auto& it = mapDelegateVoters[item];
        if(it.size() != 1) {
            mapDelegateVoters[item].erase(voter);
        } else {
            mapDelegateVoters.erase(item);    
        }
    }

    auto& setVotedDelegates = mapVoterDelegates[voter];
    if(setVotedDelegates.size() == delegates.size()) {
        mapVoterDelegates.erase(voter);
    } else {
        for(auto item : delegates)
        {
            setVotedDelegates.erase(item);
        }
    }

    return true;
}

bool Vote::ProcessRegister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height, bool fUndo)
{
    if(fUndo) {
        return ProcessUndoRegister(delegate, strDelegateName, hash, height);
    } else {
        return ProcessRegister(delegate, strDelegateName, hash, height);
    }
}

bool Vote::ProcessRegister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height)
{
    bool ret = ProcessRegister(delegate, strDelegateName);
    if(ret == false) {
        AddInvalidVote(hash, height);
        LogPrintf("ProcessRegister InvalidTx Hash:%s height:%lld\n", hash.ToString().c_str(), height);
    }

    if(ret) {
        LogPrintf("DPoS ProcessRegister: Height:%u %s ---> %s success\n", height, CBitcoinAddress(delegate).ToString().c_str(), strDelegateName.c_str());
    } else {
        LogPrintf("DPoS ProcessRegister: Height:%u %s ---> %s fail\n", height, CBitcoinAddress(delegate).ToString().c_str(), strDelegateName.c_str());
    }

    return ret;
}

bool Vote::ProcessUndoRegister(CKeyID& delegate, const std::string& strDelegateName, uint256 hash, uint64_t height)
{
    if(FindInvalidVote(hash)) {
        LogPrintf("ProcessUndoRegister InvalidTx Hash:%s height:%lld\n", hash.ToString().c_str(), height);
        return true;
    }

    bool ret = ProcessUnregister(delegate, strDelegateName);

    if(ret) {
        LogPrintf("DPoS ProcessUndoRegister: Height:%u %s ---> %s success\n", height, CBitcoinAddress(delegate).ToString().c_str(), strDelegateName.c_str());
    } else {
        LogPrintf("DPoS ProcessUndoRegister: Height:%u %s ---> %s fail\n", height, CBitcoinAddress(delegate).ToString().c_str(), strDelegateName.c_str());
    }

    return ret;
}

bool Vote::ProcessRegister(CKeyID& delegate, const std::string& strDelegateName)
{
    write_lock w(lockVote);
    if(mapDelegateName.find(delegate) != mapDelegateName.end())
        return false;
    
    if(mapNameDelegate.find(strDelegateName) != mapNameDelegate.end())
        return false;

    mapDelegateName.insert(std::make_pair(delegate, strDelegateName));
    mapNameDelegate.insert(std::make_pair(strDelegateName, delegate));
    return true;
}

bool Vote::ProcessUnregister(CKeyID& delegate, const std::string& strDelegateName)
{
    write_lock w(lockVote);
    if(mapDelegateName.find(delegate) == mapDelegateName.end())
        return false;
    
    if(mapNameDelegate.find(strDelegateName) == mapNameDelegate.end())
        return false;

    mapDelegateName.erase(delegate);
    mapNameDelegate.erase(strDelegateName);
    return true;
}

uint64_t Vote::GetDelegateVotes(const CKeyID& delegate)
{
    read_lock r(lockVote);
    return _GetDelegateVotes(delegate);
}

uint64_t Vote::_GetDelegateVotes(const CKeyID& delegate)
{
    uint64_t votes = 0;
    auto it = mapDelegateVoters.find(delegate);
    if(it != mapDelegateVoters.end()) {
        for(auto item : it->second) {
            votes += _GetAddressBalance(CMyAddress(item, CChainParams::PUBKEY_ADDRESS));
        }
    }

    return votes;
}

std::set<CKeyID> Vote::GetDelegateVoters(CKeyID& delegate)
{
    read_lock r(lockVote);
    std::set<CKeyID> s;
    auto it = mapDelegateVoters.find(delegate);
    if(it != mapDelegateVoters.end()) {
        s = it->second;
    }

    return s;
}

CKeyID Vote::GetDelegate(const std::string& name)
{
    read_lock r(lockVote);
    auto it = mapNameDelegate.find(name);
    if(it != mapNameDelegate.end())
        return it->second;
    return CKeyID();
}

std::string Vote::GetDelegate(CKeyID keyid)
{
    read_lock r(lockVote);
    auto it = mapDelegateName.find(keyid);
    if(it != mapDelegateName.end())
        return it->second;
    return std::string();
}

bool Vote::HaveVote(CKeyID voter, CKeyID delegate)
{
    bool ret = false;
    read_lock r(lockVote);
    auto it = mapDelegateVoters.find(delegate);
    if(it != mapDelegateVoters.end()) {
        if(it->second.find(voter) != it->second.end()) {
            ret = true;    
        }
    }

    return ret;
}

bool Vote::HaveDelegate_Unlock(const std::string& name, CKeyID keyid)
{
    read_lock r(lockVote);
    if(mapNameDelegate.find(name) != mapNameDelegate.end()) {
        return false;    
    }

    if(mapDelegateName.find(keyid) != mapDelegateName.end()) {
        return false;    
    }

    return true;
}

bool Vote::HaveDelegate(const std::string& name, CKeyID keyid)
{
    read_lock r(lockVote);
    
    bool ret = false;
    auto it = mapDelegateName.find(keyid);
    if(it != mapDelegateName.end() ) {
        if(it->second == name) {
            ret = true;    
        }
    }

    return ret;
}

bool Vote::HaveDelegate(std::string name)
{
    read_lock r(lockVote);
    return mapNameDelegate.find(name) != mapNameDelegate.end();
}

bool Vote::HaveDelegate(CKeyID keyID)
{
    read_lock r(lockVote);
    return mapDelegateName.find(keyID) != mapDelegateName.end();
}

std::set<CKeyID> Vote::GetVotedDelegates(CKeyID& delegate)
{
    read_lock r(lockVote);
    std::set<CKeyID> s;
    auto it = mapVoterDelegates.find(delegate);
    if(it != mapVoterDelegates.end()) {
        s = it->second;
    }

    return s;
}

uint64_t Vote::GetDelegateFunds(const CMyAddress& address)
{
    uint64_t ret = _GetAddressBalance(address);

    auto&& i = GetDelegateMultiaddress(address);
    for(auto& j : i) {
        auto r = _GetAddressBalance(j.first);
        if(r > ret) {
            ret = r;
        }
    }

    return ret;
}

std::vector<Delegate> Vote::GetTopDelegateInfo(uint64_t nMinHoldBalance, uint32_t nDelegateNum)
{
    read_lock r(lockVote);
    std::set<std::pair<uint64_t, CKeyID>> delegates;

    for(auto item : mapDelegateVoters)
    {
        uint64_t votes = _GetDelegateVotes(item.first);
        if(GetDelegateFunds(CMyAddress(item.first, CChainParams::PUBKEY_ADDRESS)) >= nMinHoldBalance) {
            delegates.insert(std::make_pair(votes, item.first));
        }
    }

    for(auto it = mapDelegateName.rbegin(); it != mapDelegateName.rend(); ++it)
    {
        if(GetDelegateFunds(CMyAddress(it->first, CChainParams::PUBKEY_ADDRESS)) < nMinHoldBalance) {
            continue;
        }

        if(delegates.size() >= nDelegateNum) {
            break;    
        }

        if(mapDelegateVoters.find(it->first) == mapDelegateVoters.end())
            delegates.insert(std::make_pair(0, it->first));
    }

    std::vector<Delegate> result;
    for(auto it = delegates.rbegin(); it != delegates.rend(); ++it)
    {
        if(result.size() >= nDelegateNum) {
            break;    
        }

        result.push_back(Delegate(it->second, it->first));
    }

    return result;
}

std::map<std::string, CKeyID> Vote::ListDelegates()
{
    read_lock r(lockVote);
    return mapNameDelegate;
}

void Vote::Delete(const std::string& strBlockHash)
{
    remove((strDelegateFileName + "-" + strBlockHash).c_str());
    remove((strVoteFileName + "-" + strBlockHash) .c_str());
    remove((strBalanceFileName + "-" + strBlockHash) .c_str());
}

bool Vote::Write(const std::string& strBlockHash)
{
    FILE* file = NULL;
    uint32_t count = 0;
    uint64_t balance = 0;

    write_lock w(lockVote);

    file = fopen((strDelegateFileName + "-" + strBlockHash) .c_str(), "wb");
    for(auto item : mapDelegateName)
    {
        fwrite(item.first.begin(), sizeof(item.first), 1, file);
        count = item.second.length();
        fwrite(&count, sizeof(count), 1, file);
        fwrite(item.second.c_str(), item.second.length(), 1, file);
    }
    fclose(file);

    file = fopen((strVoteFileName + "-" + strBlockHash).c_str(), "wb");
    for(auto item : mapDelegateVoters)
    {
        fwrite(item.first.begin(), sizeof(item.first), 1, file);
        count = item.second.size();
        fwrite(&count, sizeof(count), 1, file);
        for(auto i : item.second) {
            fwrite(i.begin(), sizeof(i), 1, file);
        }
    }
    fclose(file);

    file = fopen((strInvalidVoteTxFileName + "-" + strBlockHash).c_str(), "wb");
    for(auto item : mapHashHeightInvalidVote)
    {
        auto strHash = item.first.GetHex();
        fwrite(strHash.c_str(), strHash.length(), 1, file);
        fwrite(&item.second, sizeof(item.second), 1, file);
    }
    fclose(file);

    file = fopen((strBalanceFileName + "-" + strBlockHash).c_str(), "wb");
    for(auto item : mapAddressBalance)
    {
        balance = item.second;
        if(balance == 0)
            continue;
        fwrite(&item.first.second, sizeof(item.first.second), 1, file);
        fwrite(item.first.first.begin(), sizeof(item.first.first), 1, file);
        fwrite(&balance, sizeof(balance), 1, file);
    }
    fclose(file);

    file = fopen((strDelegateMultiaddressName + "-" + strBlockHash).c_str(), "wb");
    for(auto item : mapDelegateMultiaddress)
    {
        fwrite(&item.first.second, sizeof(item.first.second), 1, file);
        fwrite(item.first.first.begin(), sizeof(item.first.first), 1, file);

        count = item.second.size();
        fwrite(&count, sizeof(count), 1, file);
        for(auto i : item.second) {
            fwrite(&i.first.second, sizeof(i.first.second), 1, file);
            fwrite(i.first.first.begin(), sizeof(i.first.first), 1, file);

            auto strHash = i.second.GetHex();
            fwrite(strHash.c_str(), strHash.length(), 1, file);
        }
    }
    fclose(file);

    pbill->Save(strBillFileName);
    pcommittee->Save(strCommitteeFileName);

    return true;
}

bool Vote::Store(int64_t nBlockHeight, const std::string& strBlockHash)
{
    if(nBlockHeight == 0) {
        return true;    
    }

    if(Write(strBlockHash) == false) {
        return false;
    }

    if(WriteControlFile(nBlockHeight, strBlockHash, strControlFileName + "-temp") == false) {
        Delete(strBlockHash);
        return false;
    }

    int64_t nBlockHeightTemp = 0;
    std::string strBlockHashTemp;
    if(ReadControlFile(nBlockHeightTemp, strBlockHashTemp, strControlFileName) == true) {
        Delete(strBlockHashTemp);
    }

    rename(strControlFileName.c_str(), (strControlFileName + "-old").c_str());
    rename((strControlFileName + "-temp").c_str(), strControlFileName.c_str());
    remove((strControlFileName + "-old").c_str());

    return true;
}

bool Vote::Load(int64_t height, const std::string& strBlockHash)
{
    if(RepairFile(height, strBlockHash) == false)
        return false;

    return Read();
}

bool Vote::Read()
{
    FILE* file = NULL;
    unsigned char buff[20];
    unsigned char strHash[64];
    uint32_t count = 0;
    uint64_t balance = 0;

    write_lock w(lockVote);

    if(ReadControlFile(nOldBlockHeight, strOldBlockHash, strControlFileName) == false)
        return false;

    file = fopen((strDelegateFileName + "-" + strOldBlockHash).c_str(), "rb");
    if(file) {
    while(1)
    {
        if(fread(&buff[0], sizeof(buff), 1, file) <= 0) {
            break;    
        }

        CKeyID delegate(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));
        fread(&count, sizeof(count), 1, file);

        unsigned char name[128];
        memset(name, 0, sizeof(name));
        fread(&name[0], count, 1, file);

        std::string sname((const char*)&name[0], count);
        mapDelegateName[delegate] = sname;
        mapNameDelegate[sname] = delegate;
    }
    fclose(file);
    }

    file = fopen((strVoteFileName + "-" + strOldBlockHash).c_str(), "rb");
    if(file) {
    while(1)
    {
        if(fread(&buff[0], sizeof(buff), 1, file) <= 0) {
            break;    
        }

        CKeyID delegate(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));

        fread(&count, sizeof(count), 1, file);
        for(unsigned int i =0; i < count; ++i) {
            fread(&buff[0], sizeof(buff), 1, file);
            CKeyID voter(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));

            mapDelegateVoters[delegate].insert(voter);
            mapVoterDelegates[voter].insert(delegate);
        }
    }
    fclose(file);
    }

    file = fopen((strInvalidVoteTxFileName + "-" + strOldBlockHash).c_str(), "rb");
    if(file) {
    while(1)
    {
        if(fread(&strHash[0], 64, 1, file) <= 0) {
            break;
        }
        uint256 hash;
        hash.SetHex((const char*)&strHash[0]);

        uint64_t height;
        fread(&height, sizeof(height), 1, file);
        AddInvalidVote(hash, height);
    }
    fclose(file);
    }

    file = fopen((strBalanceFileName + "-" + strOldBlockHash).c_str(), "rb");
    if(file) {
    while(1)
    {
        uint8_t type = CChainParams::PUBKEY_ADDRESS;
        if(nVersion < 0) {
            if(fread(&type, sizeof(type), 1, file) <= 0) {
                break;
            }
        }

        if(fread(&buff[0], sizeof(buff), 1, file) <= 0) {
            break;    
        }

        fread(&balance, sizeof(balance), 1, file);
        uint160 address(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));
        mapAddressBalance[CMyAddress(address, type)] = balance;
    }
    fclose(file);
    }

    file = fopen((strDelegateMultiaddressName + "-" + strOldBlockHash).c_str(), "rb");
    if(file) {
    while(1)
    {
        uint8_t type = CChainParams::PUBKEY_ADDRESS;
        if(fread(&type, sizeof(type), 1, file) <= 0) {
            break;
        }

        fread(&buff[0], sizeof(buff), 1, file);

        CKeyID delegate(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));
        auto& multiAddress = mapDelegateMultiaddress[make_pair(delegate, type)];

        fread(&count, sizeof(count), 1, file);
        for(unsigned int i =0; i < count; ++i) {
            if(fread(&type, sizeof(type), 1, file) <= 0) {
                break;
            }

            fread(&buff[0], sizeof(buff), 1, file);

            CKeyID delegate(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));

            if(fread(&strHash[0], 64, 1, file) <= 0) {
                break;
            }
            uint256 hash;
            hash.SetHex((const char*)&strHash[0]);

            multiAddress[make_pair(delegate, type)] = hash;
        }
    }
    fclose(file);
    }

    pbill->Load(strBillFileName);
    pcommittee->Load(strCommitteeFileName);

    return true;
}

uint64_t Vote::GetAddressBalance(const CMyAddress& address)
{
    read_lock r(lockVote);
    return _GetAddressBalance(address);
}

uint64_t Vote::_GetAddressBalance(const CMyAddress& address)
{
    auto it = mapAddressBalance.find(address);
    if(it != mapAddressBalance.end()) {
        return it->second;
    } else {
        return 0;    
    }
}

void Vote::UpdateAddressBalance(const std::vector<std::pair<CMyAddress, int64_t>>& vAddressBalance)
{
    write_lock w(lockVote);
    std::map<CMyAddress, int64_t> mapBalance;
    for(auto iter : vAddressBalance) {
        if (iter.second == 0)
            continue;

        mapBalance[iter.first] += iter.second;
    }

    for(auto iter : mapBalance)
    {
        _UpdateAddressBalance(iter.first, iter.second);
    }

    return;
}

uint64_t Vote::_UpdateAddressBalance(const CMyAddress& address, int64_t value)
{
    int64_t balance = 0;

    auto it = mapAddressBalance.find(address);
    if(it != mapAddressBalance.end()) {
        balance = it->second + value;
        if(balance < 0) {
            abort();
        }

        if(balance == 0) {
            mapAddressBalance.erase(it);        
        } else {
            it->second = balance;
        }
        return balance;
    } else {
        if(value < 0) {
            abort();
        }

        if(value == 0) {
            return 0;    
        } else {
            mapAddressBalance[address] = value;
        }

        return value;
    }
}

void Vote::DeleteInvalidVote(uint64_t height)
{
    write_lock r(lockMapHashHeightInvalidVote);
    for(auto it =  mapHashHeightInvalidVote.begin(); it != mapHashHeightInvalidVote.end();) {
        if(it->second <= height) {
            LogPrintf("DeleteInvalidVote Hash:%s Height:%llu\n", it->first.ToString().c_str(), it->second);
            it = mapHashHeightInvalidVote.erase(it);
        } else {
            ++it;
        }
    }
}

void Vote::AddInvalidVote(uint256 hash, uint64_t height)
{
    write_lock r(lockMapHashHeightInvalidVote);
    mapHashHeightInvalidVote[hash] = height;
    LogPrintf("AddInvalidVote Hash:%s Height:%llu\n", hash.ToString().c_str(), height);
}

bool Vote::FindInvalidVote(uint256 hash)
{
    read_lock r(lockMapHashHeightInvalidVote);
    return mapHashHeightInvalidVote.find(hash) != mapHashHeightInvalidVote.end();
}

std::map<CMyAddress, uint256> Vote::GetDelegateMultiaddress(const CMyAddress& delegate)
{
    read_lock r(lockVote);

    auto it = mapDelegateMultiaddress.find(delegate);
    if(it != mapDelegateMultiaddress.end()) {
        return mapDelegateMultiaddress[delegate];
    } else {
        return std::map<CMyAddress, uint256>();
    }
}

bool Vote::AddDelegateMultiaddress(const CMyAddress& delegate, const CMyAddress& multiAddress, const uint256& txid)
{
    bool ret = false;
    write_lock r(lockVote);

    if(mapDelegateName.find(delegate.first) == mapDelegateName.end())
        return false;


    auto it = mapDelegateMultiaddress.find(delegate);
    if(it != mapDelegateMultiaddress.end()) {
        auto i = it->second.find(multiAddress);
        if(i == it->second.end()) {
            it->second.insert(std::make_pair(multiAddress, txid));
            ret = true;
        }
    } else {
        auto& i = mapDelegateMultiaddress[delegate];
        i[multiAddress] = txid;
        ret = true;
    }

    return ret;
}

bool Vote::DelDelegateMultiaddress(const CMyAddress& delegate, const CMyAddress& multiAddress, const uint256& txid)
{
    bool ret = false;
    write_lock r(lockVote);

    auto it = mapDelegateMultiaddress.find(delegate);
    if(it != mapDelegateMultiaddress.end()) {
        auto i = it->second.find(multiAddress);
        if(i != it->second.end()) {
            if(i->second == txid) {
                it->second.erase(i);

                if(it->second.empty()) {
                    mapDelegateMultiaddress.erase(it);
                }
                ret = true;
            }
        }
    }

    return ret;
}

std::multimap<uint64_t, CMyAddress> Vote::GetCoinRank(int num)
{
    std::multimap<uint64_t, CMyAddress> result;

    read_lock r(lockVote);
    for(auto& item : mapAddressBalance) {
        result.insert(make_pair(item.second, item.first));
        if(result.size() > (uint32_t)num) {
            result.erase(result.begin());
        }
    }

    return result;
}

std::map<uint64_t, std::pair<uint64_t, uint64_t>> Vote::GetCoinDistribution(const std::set<uint64_t>& arg)
{
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> result;

    read_lock r(lockVote);

    for(auto& it : arg) {
        result[it] = std::make_pair(0, 0);
    }

    for(auto& item : mapAddressBalance) {
        for(auto& it : result) {
            if(item.second <= it.first) {
                it.second.first++;
                it.second.second+= item.second;
                break;
            }
        }
    }

    return result;
}

std::vector<unsigned char> StructToData(const CRegisterCommitteeData& data)
{
    CScript script(data.opcode);
    script << ToByteVector(data.name) << ToByteVector(data.url);
  
    return ToByteVector(script);
}

std::vector<unsigned char> StructToData(const CVoteCommitteeData& data)
{
    CScript script(data.opcode);
    script << ToByteVector(data.committee);

    return ToByteVector(script);
}

std::vector<unsigned char> StructToData(const CCancelVoteCommitteeData& data)
{
    CScript script(data.opcode);
    script << ToByteVector(data.committee);

    return ToByteVector(script);
}

std::vector<unsigned char> StructToData(const CSubmitBillData& data)
{
    CScript script(data.opcode);
    script << ToByteVector(data.title) << ToByteVector(data.detail) << ToByteVector(data.url) << ToByteVector(std::to_string(data.endtime)) << (uint8_t)data.options.size();
    for(auto& it : data.options) {
        script << ToByteVector(it);
    }

    return ToByteVector(script);
}

std::vector<unsigned char> StructToData(const CVoteBillData& data)
{
    CScript script(data.opcode);
    script << ToByteVector(data.id.GetHex()) << (uint8_t)data.index;

    return ToByteVector(script);
}

string CheckStruct(const CSubmitBillData& data)
{
    string ret;
    if(data.title.empty()) {
        ret = "bill title is empty";
    } else if(data.title.length() > 128) {
        ret = "bill title length greater than 128 bytes";
    } else if(data.detail.length() > 256) {
        ret = "bill detail length greater than 256 bytes";
    } else if(data.url.empty()) {
        ret = "bill url is empty";
    } else if(data.url.length() > 256) {
        ret = "bill url length greater than 256 bytes";
    } else if(data.options.size() < 2) {
        ret = "bill options number less than 2";
    } else if(data.options.size() > 8) {
        ret = "bill options number greater than 8";
    } else {
        for(auto& i : data.options) {
            if(i.size() > 256) {
                ret = "bill option content length greater than 256 bytes";
                break;
            }
        }
    }

    return ret;
}

bool DataToStruct(CSubmitBillData& data, const CScript& script)
{
    bool ret = false;

    auto iter = script.begin();
    data.opcode = *iter++;

    opcodetype opcode;
    std::vector<unsigned char> vchRet;
    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }
    data.title = std::string(vchRet.begin(), vchRet.end());

    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }
    data.detail = std::string(vchRet.begin(), vchRet.end());

    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }
    data.url = std::string(vchRet.begin(), vchRet.end());

    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }
    data.endtime = std::stoll(std::string(vchRet.begin(), vchRet.end()));

    uint8_t num = *iter++;
    for(int i =0; i < num; ++i) {
        if (!script.GetOp2(iter, opcode, &vchRet)) {
            return false;
        }
        data.options.push_back(std::string(vchRet.begin(), vchRet.end()));
    }

    ret = true;
    return ret;
}

bool DataToStruct(CVoteBillData& data, const CScript& script)
{
    bool ret = false;

    auto iter = script.begin();
    data.opcode = *iter++;

    opcodetype opcode;
    std::vector<unsigned char> vchRet;
    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }

    if(vchRet.size() != 40) {
        return false;
    }
    data.id.SetHex(std::string(vchRet.begin(), vchRet.end()));
    data.index = *iter++;

    ret = true;
    return ret;
}

bool DataToStruct(CRegisterCommitteeData& data, const CScript& script)
{
    auto iter = script.begin();
    data.opcode = *iter++;

    opcodetype opcode;
    std::vector<unsigned char> vchRet;
    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }
    data.name = std::string(vchRet.begin(), vchRet.end());
    if(data.name.size() > 32) {
        return false;
    }

    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }
    data.url = std::string(vchRet.begin(), vchRet.end());
    if(data.url.size() > 256) {
        return false;
    }
    return true;
}

bool DataToStruct(CVoteCommitteeData& data, const CScript& script)
{
    bool ret = false;

    auto iter = script.begin();
    data.opcode = *iter++;

    opcodetype opcode;
    std::vector<unsigned char> vchRet;
    if (!script.GetOp2(iter, opcode, &vchRet)) {
        return false;
    }

    data.committee = CKeyID(uint160(vchRet));

    ret = true;
    return ret;
}
