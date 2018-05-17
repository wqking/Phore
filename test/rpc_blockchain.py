#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RPCs related to blockchainstate.

Test the following RPCs:
    - getblockchaininfo
    - gettxoutsetinfo
    - getdifficulty
    - getbestblockhash
    - getblockhash
    - getblockheader
    - getchaintxstats
    - getnetworkhashps
    - verifychain

Tests correspond to code in rpc/blockchain.cpp.
"""

from decimal import Decimal
import http.client
import subprocess

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises,
    assert_raises_rpc_error,
    assert_is_hex_string,
    assert_is_hash_string,
)
from test_framework.blocktools import (
    create_block,
    create_coinbase,
)
from test_framework.messages import (
    msg_block,
)
from test_framework.mininode import (
    P2PInterface,
    network_thread_start,
)


class BlockchainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-stopatheight=207', '-prune=1']]

    def run_test(self):
        self._test_getblockchaininfo()
        self._test_gettxoutsetinfo()
        self._test_getblockheader()
        self._test_stopatheight()
        assert self.nodes[0].verifychain(4, 0)

    def _test_getblockchaininfo(self):
        self.log.info("Test getblockchaininfo")

        keys = [
            'bestblockhash',
            'blocks',
            'chain',
            'chainwork',
            'difficulty',
            'headers',
            'verificationprogress',
        ]
        res = self.nodes[0].getblockchaininfo()
        
        # result should have these additional pruning keys if manual pruning is enabled
        assert_equal(sorted(res.keys()), sorted(keys))

    def _test_gettxoutsetinfo(self):
        node = self.nodes[0]
        res = node.gettxoutsetinfo()

        assert_equal(res['total_amount'], Decimal('17997500.00000000'))
        assert_equal(res['transactions'], 200)
        assert_equal(res['height'], 200)
        assert_equal(res['txouts'], 200)
        assert_equal(res['bestblock'], node.getblockhash(200))
        assert_equal(len(res['bestblock']), 64)

        self.log.info("Test that gettxoutsetinfo() works for blockchain with just the genesis block")
        b1hash = node.getblockhash(1)
        node.invalidateblock(b1hash)

        res2 = node.gettxoutsetinfo()
        assert_equal(res2['transactions'], 0)
        assert_equal(res2['total_amount'], Decimal('0'))
        assert_equal(res2['height'], 0)
        assert_equal(res2['txouts'], 0)
        assert_equal(res2['bestblock'], node.getblockhash(0))
        
        return

        self.log.info("Test that gettxoutsetinfo() returns the same result after invalidate/reconsider block")
        node.reconsiderblock(b1hash)

        res3 = node.gettxoutsetinfo()
        assert_equal(res['total_amount'], res3['total_amount'])
        assert_equal(res['transactions'], res3['transactions'])
        assert_equal(res['height'], res3['height'])
        assert_equal(res['txouts'], res3['txouts'])
        assert_equal(res['bestblock'], res3['bestblock'])

    def _test_getblockheader(self):
        node = self.nodes[0]

        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, "nonsense")

        node.setgenerate(True, 200)
        besthash = node.getbestblockhash()
        secondbesthash = node.getblockhash(199)
        header = node.getblockheader(besthash)

        assert_equal(header['hash'], besthash)
        assert_equal(header['height'], 200)
        assert_equal(header['confirmations'], 1)
        assert_equal(header['previousblockhash'], secondbesthash)
        assert_is_hex_string(header['chainwork'])
        assert_is_hash_string(header['hash'])
        assert_is_hash_string(header['previousblockhash'])
        assert_is_hash_string(header['merkleroot'])
        assert_is_hash_string(header['bits'], length=None)
        assert isinstance(header['time'], int)
        assert isinstance(header['nonce'], int)
        assert isinstance(header['version'], int)
        assert isinstance(header['difficulty'], Decimal)

    def _test_stopatheight(self):
        assert_equal(self.nodes[0].getblockcount(), 200)
        self.nodes[0].setgenerate(True, 6)
        assert_equal(self.nodes[0].getblockcount(), 206)
        self.log.debug('Node should not stop at this height')
        assert_raises(subprocess.TimeoutExpired, lambda: self.nodes[0].process.wait(timeout=3))
        try:
            self.nodes[0].setgenerate(True, 1)
        except (ConnectionError, http.client.BadStatusLine):
            pass  # The node already shut down before response
        self.log.debug('Node should stop at this height...')
        self.nodes[0].stop_node()
        self.nodes[0].wait_until_stopped()
        self.start_node(0)
        assert_equal(self.nodes[0].getblockcount(), 207)

if __name__ == '__main__':
    BlockchainTest().main()
