/*
 * =====================================================================================
 *
 *       Filename: fixedbitmap.hpp
 *        Created: 09/29/2019 06:20:43
 *  Last Modified: 09/29/2019 06:22:02
 *
 *    Description: 
 *
 *        Version: 1.0
 *       Revision: none
 *       Compiler: gcc
 *
 *         Author: ANHONG
 *          Email: anhonghe@gmail.com
 *   Organization: USTC
 *
 * =====================================================================================
 */

#include <vector>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

class fixedIdset final
{
    // three types of id block
    // 1. single id block, takes only 1 word: [singleId]
    // 2. continuous id block, takes only 2 words: [firstId, idCount * 2]
    // 3. bitmask id block, takes at least 2 words: [firstId, 1 | bitmask_1, bitmask_2 ...]

    private:
        // don't add extra member variable
        // compact design to make it's easy for fvFile ser/des.
        std::vector<size_t> m_idOff;
        std::vector<uint64_t> m_idBuf;

    public:
        fixedIdset() = default;

    public:
        template<typename forwardCIterator> fixedIdset(forwardCIterator ibegin, forwardCIterator iend)
        {
            push(*(ibegin++));
            for(auto p = ibegin; p != iend; ++p){
                innPush(*p);
            }
        }

    public:
        ~fixedIdset() = default;

    public:
        size_t count() const
        {
            return m_idBuf.empty() ? 0 : m_idBuf[0];
        }

        bool empty() const
        {
            return m_idBuf.empty();
        }

    public:
        void push(uint64_t id)
        {
            if(empty()){
                m_idBuf.assign({1, id});
                m_idOff.push_back(1);
                return;
            }

            if(m_idBuf[m_idOff.back()] >= id){
                throw std::invalid_argument(std::string("fixedIdset::push(") + std::to_string(id) + "): id is not in ascending order");
            }

            // TODO
            // need more check inside the block
            // 1. bitmask block
            // 2. continuous id block

            innPush(id);
        }

    private:
        void innPush(uint64_t id)
        {
            m_idBuf[0]++;
            const uint64_t idDiff = id - m_idBuf[m_idOff.back()];

            switch(const size_t bufSize = m_idBuf.size() - m_idOff.back() - 1){
                case 0:
                    {
                        // even the idDiff is 1
                        // we still convert it to a bitmask id block

                        if(idDiff < 64){
                            m_idBuf.push_back(kthBitU64(0) | kthBitU64(idDiff));
                            return;
                        }

                        // we can choose
                        // 1. add 2 words as bitmask, or
                        // 2. add 1 word to m_idBuf and 1 word to m_idOff

                        // method 1 is better than 2 since it doesn't add one more id block
                        // and it may be able to hold next potential id

                        if(idDiff < 128){
                            m_idBuf.insert(m_idBuf.end(), {kthBitU64(0), kthBitU64(idDiff - 64)});
                            return;
                        }

                        // idDiff >= 128, needs at least 3 words if using bitmask
                        // prefer to add a single id block

                        m_idOff.push_back(m_idBuf.size());
                        m_idBuf.push_back(id);
                        return;
                    }
                case 1:
                    {
                        // the id block has 1 word
                        // this 1 word could be bitmask or be continuous id count

                        // reserve the LSB of this 1 word
                        // if set then this word is a bitmask, otherwise 2 * idCount

                        const uint64_t idOff1Val = m_idBuf[m_idOff.back() + 1];
                        if(idOff1Val & 1){

                            // this is a bitmask word
                            // check idDiff and allocate more bitmask word if needed

                            if(idDiff < 192){

                                // there is already 1 word of bitmask
                                // extend if need more

                                if(idDiff >= 1 * 64){
                                    m_idBuf.resize((m_idBuf.size() - 1) + (1 + idDiff / 64));
                                }

                                m_idBuf[m_idOff.back() + 1 + idDiff / 64] |= kthBitU64(idDiff % 64);
                                return;
                            }

                            // have to add at least 3 words
                            // would better to add a new id block

                            m_idOff.push_back(m_idBuf.size());
                            m_idBuf.push_back(id);
                            return;
                        }

                        // continuous id block
                        // the +1 location is idCount

                        if(idDiff == (idOff1Val / 2)){
                            m_idBuf[m_idOff.back() + 1] += 2;
                            return;
                        }

                        // the new id is not at end of the continuous id block
                        // add a new 1 word id block

                        m_idOff.push_back(m_idBuf.size());
                        m_idBuf.push_back(id);
                        return;
                    }
                default:
                    {
                        // has more than 2 words of bitmask
                        // here there is possibility to merge continuous id block

                        if((idDiff + 1 == bufSize * 64) && (bufSize >= 4)){

                            // we can make more strict to get here
                            // like check if m_idBuf.back() == 0X7FFFFFFFFFFFFFFF

                            m_idBuf.back() |= kthBitU64(63);
                            const size_t fullCount = countFullMask(bufSize);

                            // if we have 3 fullMask, we can add a new continuous id block
                            // or wait till we have 4 fullMask, current implementation wait for 4 fullMask

                            if(fullCount >= 4){
                                m_idBuf.resize(m_idBuf.size() - fullCount);

                                // if all bullMasks
                                // then we don't need to create a new block
                                // if we directly create a continuous id block then the firstId saved twice

                                if(fullCount == bufSize){
                                    m_idBuf.push_back((fullCount * 64) * 2);
                                }

                                // only tailing fullMasks
                                // create a new continuous id block

                                else{
                                    const uint64_t firstId = m_idBuf[m_idOff.back()];
                                    m_idOff.push_back(m_idBuf.size());
                                    m_idBuf.push_back(firstId + (bufSize - fullCount) * 64);
                                    m_idBuf.push_back((fullCount * 64) * 2);
                                }
                            }
                            return;
                        }

                        // not adding last bit
                        // normal flow, this is the same in other cases

                        if(idDiff < (bufSize + 2) * 64){

                            // there is already bufSize words of bitmask
                            // extend if we need (at most 2) more

                            if(idDiff >= bufSize * 64){
                                m_idBuf.resize((m_idBuf.size() - bufSize) + (1 + idDiff / 64));
                            }

                            m_idBuf[m_idOff.back() + 1 + idDiff / 64] |= kthBitU64(idDiff % 64);
                            return;
                        }

                        m_idOff.push_back(m_idBuf.size());
                        m_idBuf.push_back(id);
                        return;
                    }
            }
        }

    public:
        void getIds(std::vector<uint64_t> &v) const
        {
            v.clear();
            v.reserve(count());

            for(size_t i = 0; i < m_idOff.size(); ++i){
                innGetBlockIds(v, i);
            }
        }

    public:
        bool hasId(uint64_t id) const
        {
            auto p = std::upper_bound(m_idOff.begin(), m_idOff.end(), id, [this](uint64_t parmId, size_t off) -> bool
            {
                // check: https://zh.cppreference.com/w/cpp/algorithm/upper_bound
                // parameter of upper_bound() is very different to lower_bound
                return parmId < m_idBuf[off];
            });

            if(p == m_idOff.begin()){
                return false;
            }

            const size_t idIndex = std::distance(m_idOff.begin(), p) - 1;
            const size_t bufCount = [this, idIndex]()
            {
                if(idIndex + 1 < m_idOff.size()){
                    return m_idOff[idIndex + 1] - m_idOff[idIndex] - 1;
                }
                return m_idBuf.size() - m_idOff.back() - 1;
            }();

            if(bufCount == 0){
                return m_idBuf[m_idOff[idIndex]] == id;
            }

            const uint64_t firstId = m_idBuf[m_idOff[idIndex]];
            const uint64_t idOff1Val = m_idBuf[m_idOff[idIndex] + 1];

            if(!(idOff1Val & 1)){
                return id >= firstId && id < firstId + (idOff1Val / 2);
            }

            const uint64_t idDiff = id - firstId;
            return (idDiff < bufCount * 64) && (m_idBuf[m_idOff[idIndex] + 1 + idDiff / 64] & kthBitU64(idDiff % 64));
        }

    private:
        void innGetBlockIds(std::vector<uint64_t> &v, size_t idIndex) const
        {
            const size_t bufCount = [this, idIndex]()
            {
                if(idIndex + 1 < m_idOff.size()){
                    return m_idOff[idIndex + 1] - m_idOff[idIndex] - 1;
                }
                return m_idBuf.size() - m_idOff.back() - 1;
            }();

            if(bufCount == 0){
                v.push_back(m_idBuf[m_idOff[idIndex]]);
                return;
            }

            const uint64_t firstId = m_idBuf[m_idOff[idIndex]];
            const uint64_t idOff1Val = m_idBuf[m_idOff[idIndex] + 1];

            if(!(idOff1Val & 1)){
                for(size_t i = 0, idCount = idOff1Val / 2; i < idCount; ++i){
                    v.push_back(firstId + i);
                }
                return;
            }

            // bitmask id block

            for(size_t i = 0; i < bufCount * 64; ++i){
                if(m_idBuf[m_idOff[idIndex] + 1 + i / 64] & kthBitU64(i % 64)){
                    v.push_back(firstId + i);
                }
            }
        }

    private:
        size_t countFullMask(size_t bufSize) const
        {
            size_t fullCount = 0;
            for(size_t i = 0; i < bufSize; ++i){
                if(*(m_idBuf.rbegin() + i) != (uint64_t)(-1)){
                    break;
                }
                fullCount++;
            }
            return fullCount;
        }

    private:
        static constexpr uint64_t kthBitU64(int off)
        {
            return (uint64_t)(1) << off;
        }

    public:
        std::vector<uint64_t> &idOff()
        {
            return m_idOff;
        }

        std::vector<uint64_t> &idBuf()
        {
            return m_idBuf;
        }

        const std::vector<uint64_t> &idOff() const
        {
            return m_idOff;
        }

        const std::vector<uint64_t> &idBuf() const
        {
            return m_idBuf;
        }
};
