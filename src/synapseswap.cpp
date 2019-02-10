// This file is included in init.cpp directly to avoid touching the build system

#include "leveldbwrapper.h"
#include "uint256.h"
#include "coins.h"
#include "hash.h"

#include <memory>
#include <vector>
#include <algorithm>

#include <iostream>

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
	UtxoIterator();
	
	UtxoIteratorItem next();

private:
    CLevelDBWrapper db;
    std::unique_ptr<leveldb::Iterator> iterator;
	int debugCounter;
};

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

uint256 computeHashAmount(const uint256 & txID, const CAmount amount)
{
	CDataStream stream(0, 0);
	stream << txID;
	stream << amount;
	return Hash(stream.begin(), stream.end());
}

uint256 computeHashCoins(const uint256 & txID, const CCoins & coins)
{
	return computeHashAmount(txID, getUnspentAmount(coins));
}

UtxoIterator::UtxoIterator()
	:
		db(GetDataDir() / "chainstate", 50 * 1024 * 1024, false, false),
		iterator(db.NewIterator())
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


class SynapseSwap
{
private:
	using HashList = std::vector<uint256>;

public:
	SynapseSwap();

	void debugTest();
	void dumpUtxo();
	uint256 computeMerkleRoot();
	ProofList getProof(const uint256 & tx);
	uint256 computeProofRoot(const uint256 & tx, const ProofList & proof);

private:
	uint256 getProofHash(const HashList & hashList, const int index);
	int buildHashList(HashList & hashList, const uint256 * txToProof);
	void moveUp(HashList & hashList);

private:
};

SynapseSwap::SynapseSwap()
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
	UtxoIterator iterator;
	
	for(;;) {
		UtxoIteratorItem item = iterator.next();
		if(! item.isValid()) {
			break;
		}
		hashList.push_back(computeHashCoins(item.txid, item.coins));
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

ProofList SynapseSwap::getProof(const uint256 & tx)
{
	HashList hashList;
	uint256 proofHash;
	int index = buildHashList(hashList, &tx);
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

uint256 SynapseSwap::computeProofRoot(const uint256 & tx, const ProofList & proof)
{
	uint256 hash = tx;
	
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

void SynapseSwap::debugTest()
{
	dumpUtxo();

	uint256 tx("a0aeae14bba0d7c6f542cd8bdde7dd01bbf5b74e98003cd89ec9a7afc4940a00");
	auto hash = computeHashAmount(tx, 420000000);
	ProofList proof = getProof(hash);
	for(const auto & node : proof) std::cout << "path: " << node.hash.GetHex() << " " << (node.left ? "left" : "right") << std::endl;

	uint256 proofRoot = computeProofRoot(hash, proof);
	std::cout << "proofRoot: " << proofRoot.GetHex() << std::endl;

	std::cout << "Root: " << computeMerkleRoot().GetHex() << std::endl;
}

void SynapseSwap::dumpUtxo()
{
	UtxoIterator iterator;
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


} //namespace synapseswap
