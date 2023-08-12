/* Copyright (c) 2019 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation, nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef LOC_SKIP_LIST_H
#define LOC_SKIP_LIST_H

#include <stdlib.h>
#include <list>
#include <vector>
#include <iostream>
#include <algorithm>

using namespace std;

namespace loc_util {

template <typename T,
         template<typename elem, typename Allocator = std::allocator<elem>> class container = list>
class SkipNode {
public:
    typedef typename container<SkipNode<T, container>>::iterator NodeIterator;

    int mLevel;
    T mData;
    NodeIterator mNextInLevel;

    SkipNode(int level, T& data): mLevel(level), mData(data) {}
};

template <typename T>
class SkipList {
    using NodeIterator = typename SkipNode<T>::NodeIterator;
private:
    list<SkipNode<T>> mMainList;
    vector<NodeIterator> mHeadVec;
    vector<NodeIterator> mTailVec;
public:
    SkipList(int totalLevels);
    void append(T& data, int level);
    void pop(int level);
    void pop();
    T front(int level);
    int size();
    void flush();
    list<pair<T, int>> dump();
    list<pair<T, int>> dump(int level);
};

template <typename T>
SkipList<T>::SkipList(int totalLevels): mHeadVec(totalLevels, mMainList.end()),
        mTailVec(totalLevels, mMainList.end()) {}

template <typename T>
void SkipList<T>::append(T& data, int level) {
    if ( level < 0 || level >= mHeadVec.size()) {
        return;
    }

    SkipNode<T> node(level, data);
    node.mNextInLevel = mMainList.end();
    mMainList.push_back(node);
    auto iter = --mMainList.end();
    if (mHeadVec[level] == mMainList.end()) {
        mHeadVec[level] = iter;
    } else {
        (*mTailVec[level]).mNextInLevel = iter;
    }
    mTailVec[level] = iter;
}

template <typename T>
void SkipList<T>::pop(int level) {
    if (mHeadVec[level] == mMainList.end()) {
        return;
    }

    if ((*mHeadVec[level]).mNextInLevel == mMainList.end()) {
        mTailVec[level] = mMainList.end();
    }

    auto tmp_iter = (*mHeadVec[level]).mNextInLevel;
    mMainList.erase(mHeadVec[level]);
    mHeadVec[level] = tmp_iter;
}

template <typename T>
void SkipList<T>::pop() {
    pop(mMainList.front().mLevel);
}

template <typename T>
T SkipList<T>::front(int level) {
    return (*mHeadVec[level]).mData;
}

template <typename T>
int SkipList<T>::size() {
    return mMainList.size();
}

template <typename T>
void SkipList<T>::flush() {
    mMainList.clear();
    for (int i = 0; i < mHeadVec.size(); i++) {
        mHeadVec[i] = mMainList.end();
        mTailVec[i] = mMainList.end();
    }
}

template <typename T>
list<pair<T, int>> SkipList<T>::dump() {
    list<pair<T, int>> li;
    for_each(mMainList.begin(), mMainList.end(), [&](SkipNode<T> &item) {
        li.push_back(make_pair(item.mData, item.mLevel));
    });
    return li;
}

template <typename T>
list<pair<T, int>> SkipList<T>::dump(int level) {
    list<pair<T, int>> li;
    auto head = mHeadVec[level];
    while (head != mMainList.end()) {
        li.push_back(make_pair((*head).mData, (*head).mLevel));
        head = (*head).mNextInLevel;
    }
    return li;
}

}

#endif
