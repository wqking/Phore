// This file is included in init.cpp directly to avoid touching the build system

#include "leveldbwrapper.h"
#include "uint256.h"
#include "coins.h"
#include "hash.h"

#include <memory>
#include <vector>

#include <iostream>

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
	uint256 computeHash(const uint256 & left, const uint256 & right);
	int firstRound(HashList & txList, const uint256 & txToProof, uint256 * proofHash);
	void moveUp(HashList & txList);

private:
};

SynapseSwap::SynapseSwap()
{
}

uint256 SynapseSwap::computeHash(const uint256 & left, const uint256 & right)
{
	return Hash(left.begin(), left.end(), right.begin(), right.end());
}

int SynapseSwap::firstRound(HashList & txList, const uint256 & txToProof, uint256 * proofHash)
{
	UtxoIterator iterator;
	int indexToProof = -1;
	int index = 0;
	
	for(;;) {
		UtxoIteratorItem leftItem = iterator.next();
		if(! leftItem.isValid()) {
			break;
		}
		if(leftItem.txid == txToProof) {
			indexToProof = index;
		}
		++index;

		UtxoIteratorItem rightItem = iterator.next();
		if(rightItem.isValid()) {
			if(rightItem.txid == txToProof) {
				indexToProof = index;
			}
			if(proofHash != nullptr) {
				if(indexToProof + 1 == index) {
					*proofHash = rightItem.txid;
				}
				if(indexToProof == index) {
					*proofHash = leftItem.txid;
				}
			}

			++index;
			txList.push_back(computeHash(leftItem.txid, rightItem.txid));
		}
		else {
			if(proofHash != nullptr) {
				if(indexToProof + 1 == index) {
					*proofHash = rightItem.txid;
				}
			}

			txList.push_back(computeHash(leftItem.txid, leftItem.txid));
			break;
		}
	}
	
	return indexToProof;
}

void SynapseSwap::moveUp(HashList & txList)
{
	int count = (int)txList.size();
	int halfCount = (count + 1) / 2;
	
	for(int i = 0; i < halfCount; ++i) {
		int leftIndex = i * 2;
		int rightIndex = leftIndex + 1;
		if(rightIndex >= count) {
			rightIndex = leftIndex;
		}
		txList[i] = computeHash(txList[leftIndex], txList[rightIndex]);
	}
	
	txList.resize(halfCount);
}

uint256 SynapseSwap::computeMerkleRoot()
{
	HashList txList;
	firstRound(txList, uint256(), nullptr);
	
	while(txList.size() > 1) {
		moveUp(txList);
	}
	
	if(txList.empty()) {
		return uint256();
	}
	
	return txList.front();
}

ProofList SynapseSwap::getProof(const uint256 & tx)
{
	HashList txList;
	uint256 proofHash;
	int index = firstRound(txList, tx, &proofHash);
	if(index < 0) {
		return ProofList();
	}
	
	ProofList proof;

	proof.push_back({
		(index & 1) > 0,
		proofHash
	});

	for(;;) {
		index >>= 1;
		if((index & 1) == 0) {
			proofHash = (index + 1 >= (int)txList.size() ? txList[index] : txList[index + 1]);
		}
		else {
			proofHash = txList[index - 1];
		}

		proof.push_back({
			(index & 1) > 0,
			proofHash
		});
		
		moveUp(txList);
		
		if(txList.size() == 1) {
			break;
		}
	}
	return proof;
}

uint256 SynapseSwap::computeProofRoot(const uint256 & tx, const ProofList & proof)
{
	uint256 hash = tx;
	
	for(const auto node : proof) {
		if(node.left) {
			hash = computeHash(node.hash, hash);
		}
		else {
			hash = computeHash(hash, node.hash);
		}
	}
	
	return hash;
}

void SynapseSwap::debugTest()
{
	//HashList txList;
	//uint256 proofHash;
	//firstRound(txList, uint256("f21ea46ab91c0f7fa7829aaf3f96787c742f8ecfa709ba41ebf74d50a32e0000"), &proofHash);
	//std::cout << "proofHash: " << proofHash.GetHex() << std::endl;

	auto hash12 = computeHash(uint256("f365fec31f803294d96ad2dde5f3e847addc69c62f89828a11c6f0d1c30c0000"), uint256("04a0f88eb2f4588a6ee970c7342b2d3e52b038d4b6bb5d4f8a49ac3e9a110000"));
	auto hash34 = computeHash(uint256("2438cf72cbc602738daf645564b0015df2ccf3497c98d25600c6be6d001c0000"), uint256("f21ea46ab91c0f7fa7829aaf3f96787c742f8ecfa709ba41ebf74d50a32e0000"));
	std::cout << "Hash12: " << hash12.GetHex() << std::endl;
	std::cout << "Hash34: " << hash34.GetHex() << std::endl;
	std::cout << "Hash1234: " << computeHash(hash12, hash34).GetHex() << std::endl;
	
	uint256 tx("f21ea46ab91c0f7fa7829aaf3f96787c742f8ecfa709ba41ebf74d50a32e0000");
	ProofList proof = getProof(tx);
	for(const auto & node : proof) {
		std::cout << "path: " << node.hash.GetHex() << " " << (node.left ? "left" : "right") << std::endl;
	}
	uint256 proofRoot = computeProofRoot(tx, proof);
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
		if(count <= 100) {
			std::cout << item.txid.GetHex() << " " << item.coins.vout.size() << std::endl;
		}
	}
	std::cout << "Count: " << count << std::endl;
}
