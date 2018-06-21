
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <stdlib.h>
#include "util.h"
#include "base58.h"
#include "vote.h"

typedef boost::shared_lock<boost::shared_mutex> read_lock;
typedef boost::unique_lock<boost::shared_mutex> write_lock;

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

bool Vote::Init(int64_t nBlockHeight, const std::string& strBlockHash)
{ 
    //write_lock w(lockVote);

    strFilePath = (GetDataDir() / "dpos").string();
    strDelegateFileName = strFilePath + "/" + DELEGATE_FILE;
    strVoteFileName = strFilePath + "/" + VOTE_FILE;
    strBalanceFileName = strFilePath + "/" + BALANCE_FILE;
    strControlFileName = strFilePath + "/" + CONTROL_FILE;

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
            votes += _GetAddressBalance(item);
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

std::vector<Delegate> Vote::GetVoteInfo()
{
    read_lock r(lockVote);
    std::set<std::pair<uint64_t, CKeyID>> delegates;

    for(auto item : mapDelegateVoters)
    {
        uint64_t votes = _GetDelegateVotes(item.first);
        delegates.insert(std::make_pair(votes, item.first));
    }

    for(auto it = mapDelegateName.rbegin(); it != mapDelegateName.rend(); ++it)
    {
        if(delegates.size() >= 100) {
            break;    
        }

        if(mapDelegateVoters.find(it->first) == mapDelegateVoters.end())
            delegates.insert(std::make_pair(0, it->first));
    }

    std::vector<Delegate> result;
    for(auto it = delegates.rbegin(); it != delegates.rend(); ++it)
    {
        if(result.size() >= 100) {
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

    file = fopen((strBalanceFileName + "-" + strBlockHash).c_str(), "wb");
    for(auto item : mapAddressBalance)
    {
        balance = item.second;
        if(balance == 0)
            continue;
        fwrite(item.first.begin(), sizeof(item.first), 1, file);
        fwrite(&balance, sizeof(balance), 1, file);
    }
    fclose(file);

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
        fread(&name[0], count, 1, file);
        name[count] = 0;
        std::string sname((const char*)&name[0]);
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

    file = fopen((strBalanceFileName + "-" + strOldBlockHash).c_str(), "rb");
    if(file) {
    while(1)
    {
        if(fread(&buff[0], sizeof(buff), 1, file) <= 0) {
            break;    
        }

        CKeyID address(uint160(std::vector<unsigned char>(&buff[0], &buff[0] + sizeof(buff))));
        fread(&balance, sizeof(balance), 1, file);

        mapAddressBalance[address] = balance;
    }
    fclose(file);
    }

    return true;
}

uint64_t Vote::GetAddressBalance(const CKeyID& address)
{
    read_lock r(lockVote);
    return _GetAddressBalance(address);
}

uint64_t Vote::_GetAddressBalance(const CKeyID& address)
{
    auto it = mapAddressBalance.find(address);
    if(it != mapAddressBalance.end()) {
        return it->second;
    } else {
        return 0;    
    }
}

void Vote::UpdateAddressBalance(const std::vector<std::pair<CKeyID, int64_t>>& vAddressBalance)
{
    write_lock w(lockVote);
    std::map<CKeyID, int64_t> mapBalance;
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

uint64_t Vote::_UpdateAddressBalance(const CKeyID& address, int64_t value)
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
