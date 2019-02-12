// This file is included in init.cpp directly to avoid touching the build system

#include "leveldbwrapper.h"
#include "uint256.h"
#include "coins.h"
#include "hash.h"
#include "wallet.h"
#include "txdb.h"

#include <memory>
#include <vector>
#include <algorithm>

#include <iostream>

extern CWallet* pwalletMain;

namespace synapseswap {

struct UtxoIteratorItem
{
	uint256 txid;
	CCoins coins;
	
	bool isValid() const {
		return ! txid.IsNull();
	}
};

struct UtxoProofNode
{
	bool left;
	uint256 hash;
};
using ProofList = std::vector<UtxoProofNode>;

class UtxoIterator
{
public:
	explicit UtxoIterator(CLevelDBWrapper * db);
	
	UtxoIteratorItem next();

private:
    CLevelDBWrapper * db;
    std::unique_ptr<leveldb::Iterator> iterator;
	int debugCounter;
};

std::string binToHex(const void * p, const size_t length)
{
	char psz[1024 * 2 + 1];
	for(size_t i = 0; i < length; i++)
		sprintf(psz + i * 2, "%02x", ((unsigned char*)p)[i]);
	return std::string(psz, psz + length * 2);
}

std::string binToReversedHex(const void * p, const size_t length)
{
	char psz[1024 * 2 + 1];
	for(size_t i = 0; i < length; i++)
		sprintf(psz + i * 2, "%02x", ((unsigned char*)p)[length - i - 1]);
	return std::string(psz, psz + length * 2);
}

template <typename C>
std::string binToHex(const C & c)
{
	return binToHex(&c[0], c.size());
}

template <typename C>
std::string binToReversedHex(const C & c)
{
	return binToReversedHex(&c[0], c.size());
}

CAmount getUnspentAmount(const CCoins & coin)
{
	CAmount amount = 0;
	
	for(const auto & txOut : coin.vout) {
		if(! txOut.IsNull() && txOut.nValue > 0) {
			amount += txOut.nValue;
		}
	}
	
	return amount;
}

uint256 computeHashes(const uint256 & left, const uint256 & right)
{
	return Hash(left.begin(), left.end(), right.begin(), right.end());
}

uint256 computeHashTxOut(const CTxOut & out)
{
	CDataStream stream(0, 0);
	stream << out.scriptPubKey;
	stream << out.nValue;
	return Hash(stream.begin(), stream.end());
}

uint256 checkedComputeHashTxOut(const CTxOut & out)
{
	if(out.IsNull()) {
		return uint256();
	}
	
	return computeHashTxOut(out);
}

UtxoIterator::UtxoIterator(CLevelDBWrapper * db)
	:
		db(db),
		iterator(db->NewIterator())
{
	iterator->SeekToFirst();
	
	debugCounter = 0;
}

UtxoIteratorItem UtxoIterator::next()
{
	for(; iterator->Valid(); iterator->Next()) {
		if(iterator->key().size() != 33) {
			continue;
		}

		//++debugCounter; if(debugCounter > 4) break;

        CDataStream keyStream(iterator->key().data(), iterator->key().data() + iterator->key().size(), SER_DISK, CLIENT_VERSION);
        std::pair<char, uint256> keyPair;
        keyStream >> keyPair;
		
		if(keyPair.first != 'c') {
			continue;
		}

        CDataStream valueStream(iterator->value().data(), iterator->value().data() + iterator->value().size(), SER_DISK, CLIENT_VERSION);
        CCoins coins;
        valueStream >> coins;
        
        iterator->Next();
        
        if(getUnspentAmount(coins) <= 0) {
			continue;
		}
        
        return UtxoIteratorItem {
			keyPair.second,
			coins
		};
	}
	
	return UtxoIteratorItem();
}

// This is a hack to access protected member 'db' in CCoinsViewDB
// It's UB according to C++ standard, but it should work in all compilers.
class HackCoinsViewDB : public CCoinsViewDB
{
public:
	CLevelDBWrapper * getDb() {
		return &db;
	}
};

struct KeyItem
{
	CKeyID id;
	CPubKey publicKey;
	CKey key;
};

class SynapseSwap
{
private:
	using HashList = std::vector<uint256>;

public:
	SynapseSwap(CCoinsViewDB * coinsViewDB);

	void debugTest();
	void dumpUtxo();
	void dumpSignatures();
	uint256 computeMerkleRoot();
	ProofList getProof(const uint256 & hash);
	uint256 computeProofRoot(const uint256 & h, const ProofList & proof);

private:
	bool getUtxoCoins(const uint256 & txid, CCoins & coins) const;
	uint256 getProofHash(const HashList & hashList, const int index);
	int buildHashList(HashList & hashList, const uint256 * hashToProof);
	void moveUp(HashList & hashList);
	std::vector<KeyItem> getKeys();
	CWallet * getWallet() const;

private:
	CLevelDBWrapper * utxoDb;
};

SynapseSwap::SynapseSwap(CCoinsViewDB * coinsViewDB)
	: utxoDb(static_cast<HackCoinsViewDB *>(coinsViewDB)->getDb())
{
}

uint256 SynapseSwap::getProofHash(const HashList & hashList, const int index)
{
	if((index & 1) == 0) {
		return (index + 1 >= (int)hashList.size() ? hashList[index] : hashList[index + 1]);
	}
	else {
		return hashList[index - 1];
	}
}

int SynapseSwap::buildHashList(HashList & hashList, const uint256 * hashToProof)
{
	UtxoIterator iterator(utxoDb);
	
	for(;;) {
		UtxoIteratorItem item = iterator.next();
		if(! item.isValid()) {
			break;
		}
		for(const auto & out : item.coins.vout) {
			uint256 hash = checkedComputeHashTxOut(out);
			if(!hash.IsNull()) {
				hashList.push_back(hash);
			}
		}
	}
	
	std::sort(hashList.begin(), hashList.end());
	
	int indexToProof = -1;
	if(hashToProof != nullptr) {
		auto it = std::find(hashList.begin(), hashList.end(), *hashToProof);
		if(it != hashList.end()) {
			indexToProof = it - hashList.begin();
		}
	}
	
	return indexToProof;
}

void SynapseSwap::moveUp(HashList & hashList)
{
	int count = (int)hashList.size();
	int halfCount = (count + 1) / 2;
	
	for(int i = 0; i < halfCount; ++i) {
		int leftIndex = i * 2;
		int rightIndex = leftIndex + 1;
		if(rightIndex >= count) {
			rightIndex = leftIndex;
		}
		hashList[i] = computeHashes(hashList[leftIndex], hashList[rightIndex]);
	}
	
	hashList.resize(halfCount);
}

uint256 SynapseSwap::computeMerkleRoot()
{
	HashList hashList;
	buildHashList(hashList, nullptr);
	
	while(hashList.size() > 1) {
		moveUp(hashList);
	}
	
	if(hashList.empty()) {
		return uint256();
	}
	
	return hashList.front();
}

ProofList SynapseSwap::getProof(const uint256 & hash)
{
	HashList hashList;
	uint256 proofHash;
	int index = buildHashList(hashList, &hash);
	if(index < 0) {
		return ProofList();
	}
	
	ProofList proof;

	for(;;) {
		proofHash = getProofHash(hashList, index);

		proof.push_back({
			(index & 1) > 0,
			proofHash
		});
		
		moveUp(hashList);
		
		if(hashList.size() == 1) {
			break;
		}

		index >>= 1;
	}
	return proof;
}

uint256 SynapseSwap::computeProofRoot(const uint256 & h, const ProofList & proof)
{
	uint256 hash = h;
	
	for(const auto node : proof) {
		if(node.left) {
			hash = computeHashes(node.hash, hash);
		}
		else {
			hash = computeHashes(hash, node.hash);
		}
	}
	
	return hash;
}

bool SynapseSwap::getUtxoCoins(const uint256 & txid, CCoins & coins) const
{
    return utxoDb->Read(make_pair('c', txid), coins);
}

void SynapseSwap::debugTest()
{
	dumpUtxo();

	uint256 tx("cc3995305ff73fe1d7faeda0ec8c9f977fe96526d7f6b4d835c783eecdb50b00");
	CCoins coins;
	if(!getUtxoCoins(tx, coins)) {
		std::cout << "Can't find coins for " << tx.GetHex() << std::endl;
	}
	else {
		for(const auto & out : coins.vout) {
			auto hash = checkedComputeHashTxOut(out);
			if(hash.IsNull()) continue;

			ProofList proof = getProof(hash);
			for(const auto & node : proof) std::cout << "path: " << node.hash.GetHex() << " " << (node.left ? "left" : "right") << std::endl;

			uint256 proofRoot = computeProofRoot(hash, proof);
			std::cout << "proofRoot: " << proofRoot.GetHex() << std::endl;
			
			break;
		}
	}

	std::cout << "Root: " << computeMerkleRoot().GetHex() << std::endl;
	
	dumpSignatures();
	getKeys();
}

void SynapseSwap::dumpUtxo()
{
	UtxoIterator iterator(utxoDb);
	int count = 0;
	for(;;) {
		UtxoIteratorItem item = iterator.next();
		if(! item.isValid()) {
			break;
		}
		++count;
		if(count <= 10) {
			std::cout << item.txid.GetHex() << " " << getUnspentAmount(item.coins) << std::endl;
		}
	}
	std::cout << "Count: " << count << std::endl;
}

void SynapseSwap::dumpSignatures()
{
	CWallet * wallet = getWallet();

	int txCount = 0;
	LOCK2(cs_main, wallet->cs_wallet);
	for (map<uint256, CWalletTx>::const_iterator it = wallet->mapWallet.begin(); it != wallet->mapWallet.end(); ++it) {
		const uint256& wtxid = it->first;
		const CWalletTx* pcoin = &(*it).second;
		
		if(!utxoDb->Exists(make_pair('c', wtxid))) {
			continue;
		}
		
		CCoins coins;
		bool hasCoins = getUtxoCoins(wtxid, coins);
		
		if(! hasCoins) {
			continue;
		}

		++txCount;
		std::cout << "wtxid: " << wtxid.GetHex() << std::endl;
	}
	std::cout << "txCount: " << txCount << std::endl;
}

std::vector<KeyItem> SynapseSwap::getKeys()
{
	CWallet * wallet = getWallet();
	std::set<CKeyID> keyIdSet;
	wallet->GetKeys(keyIdSet);
	
	std::vector<KeyItem> keys;
	for(const CKeyID & keyId : keyIdSet) {
		KeyItem item;
		item.id = keyId;
		if(wallet->GetPubKey(keyId, item.publicKey)) {
			if(wallet->GetKey(keyId, item.key)) {
				keys.push_back(item);
/*std::cout << "item.publicKey: "
	//<< binToHex(item.publicKey.Raw())
	<< item.publicKey.GetHash().GetHex()
	<< std::endl;*/
			}
		}
	}
	return keys;
}

CWallet * SynapseSwap::getWallet() const
{
	return pwalletMain;
}

} //namespace synapseswap
