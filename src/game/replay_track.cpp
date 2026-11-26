#include "solengine/replay_track.hpp"
#include "solengine/solitaire_board.hpp"
#include "eatsbits/ui/base64.hpp"
#include <algorithm>
#include <cctype>

namespace solengine {

namespace {

class BitWriter {
public:
    void writeBits(uint32_t value, int bitCount) {
        for (int i = 0; i < bitCount; ++i) {
            const uint32_t bit = (value >> i) & 1u;
            curByte_ |= static_cast<uint8_t>(bit << bitPos_);
            ++bitPos_;
            if (bitPos_ == 8) {
                bytes_.push_back(curByte_);
                curByte_ = 0;
                bitPos_ = 0;
            }
        }
    }

    void alignByte() {
        if (bitPos_ > 0) {
            bytes_.push_back(curByte_);
            curByte_ = 0;
            bitPos_ = 0;
        }
    }

    void writeU8(uint8_t v) {
        alignByte();
        bytes_.push_back(v);
    }

    void writeU16(uint16_t v) {
        alignByte();
        bytes_.push_back(static_cast<uint8_t>(v & 0xFFu));
        bytes_.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }

    void writeU32(uint32_t v) {
        alignByte();
        bytes_.push_back(static_cast<uint8_t>(v & 0xFFu));
        bytes_.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        bytes_.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        bytes_.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }

    void writeBytes(std::string_view sv) {
        alignByte();
        for (char c : sv) {
            bytes_.push_back(static_cast<uint8_t>(c));
        }
    }

    [[nodiscard]] const std::vector<uint8_t>& bytes() const noexcept { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    uint8_t curByte_{0};
    int bitPos_{0};
};

class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& bytes) noexcept : bytes_(bytes) {}

    bool readBits(int bitCount, uint32_t& out) noexcept {
        uint32_t val = 0;
        for (int i = 0; i < bitCount; ++i) {
            if (bytePos_ >= bytes_.size()) return false;
            const uint32_t bit = (bytes_[bytePos_] >> bitPos_) & 1u;
            val |= (bit << i);
            ++bitPos_;
            if (bitPos_ == 8) {
                ++bytePos_;
                bitPos_ = 0;
            }
        }
        out = val;
        return true;
    }

    void alignByte() noexcept {
        if (bitPos_ > 0) {
            ++bytePos_;
            bitPos_ = 0;
        }
    }

    bool readU8(uint8_t& out) noexcept {
        alignByte();
        if (bytePos_ >= bytes_.size()) return false;
        out = bytes_[bytePos_++];
        return true;
    }

    bool readU16(uint16_t& out) noexcept {
        alignByte();
        if (bytePos_ + 2 > bytes_.size()) return false;
        out = static_cast<uint16_t>(bytes_[bytePos_]) |
              (static_cast<uint16_t>(bytes_[bytePos_ + 1]) << 8);
        bytePos_ += 2;
        return true;
    }

    bool readU32(uint32_t& out) noexcept {
        alignByte();
        if (bytePos_ + 4 > bytes_.size()) return false;
        out = static_cast<uint32_t>(bytes_[bytePos_]) |
              (static_cast<uint32_t>(bytes_[bytePos_ + 1]) << 8) |
              (static_cast<uint32_t>(bytes_[bytePos_ + 2]) << 16) |
              (static_cast<uint32_t>(bytes_[bytePos_ + 3]) << 24);
        bytePos_ += 4;
        return true;
    }

    bool readString(size_t len, std::string& out) {
        alignByte();
        if (bytePos_ + len > bytes_.size()) return false;
        out.assign(reinterpret_cast<const char*>(bytes_.data() + bytePos_), len);
        bytePos_ += len;
        return true;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t bytePos_{0};
    int bitPos_{0};
};

std::string sanitizePlainText(std::string_view raw, size_t maxLen) {
    std::string out;
    out.reserve(std::min(raw.size(), maxLen));
    for (size_t i = 0; i < raw.size() && out.size() < maxLen; ++i) {
        const unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (ch == '\r') continue;
        if (ch == '\n' || ch == '\t') {
            out.push_back(' ');
        } else if (ch >= 0x20u) {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::string toUrlSafeBase64(const std::vector<uint8_t>& bytes) {
    std::string b64 = eatsbits::ui::base64Encode(bytes);
    for (char& c : b64) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!b64.empty() && b64.back() == '=') {
        b64.pop_back();
    }
    return b64;
}

std::string_view extractPayload(std::string_view input) noexcept {
    // Trim leading/trailing ASCII whitespace
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
        input.remove_suffix(1);
    }
    // Support full URLs containing "#r=sol1:..." or "sol1:..."
    const size_t tagPos = input.find("sol1:");
    if (tagPos != std::string_view::npos) {
        input.remove_prefix(tagPos + 5);
        // Stop at whitespace or URL delimiter if embedded in a sentence/URL
        size_t endPos = 0;
        while (endPos < input.size()) {
            const unsigned char c = static_cast<unsigned char>(input[endPos]);
            if (std::isalnum(c) || c == '-' || c == '_' || c == '+' || c == '/' || c == '=') {
                ++endPos;
            } else {
                break;
            }
        }
        return input.substr(0, endPos);
    }
    return {};
}

} // namespace

const ReplayNote* ReplayTrack::findNote(size_t moveIndex) const noexcept {
    for (const auto& n : notes) {
        if (static_cast<size_t>(n.moveIndex) == moveIndex) {
            return &n;
        }
    }
    return nullptr;
}

void ReplayTrack::setNote(size_t moveIndex, std::string text, uint8_t noteFlags,
                          std::vector<ReplayHighlight> highlights) {
    text = sanitizePlainText(text, kMaxNoteTextLen);
    if (text.empty()) return;
    if (highlights.size() > kMaxHighlightsPerNote) {
        highlights.resize(kMaxHighlightsPerNote);
    }
    if (!highlights.empty()) {
        noteFlags |= ReplayNote_HasHighlights;
    } else {
        noteFlags &= static_cast<uint8_t>(~ReplayNote_HasHighlights);
    }
    for (auto& n : notes) {
        if (static_cast<size_t>(n.moveIndex) == moveIndex) {
            n.text = std::move(text);
            n.flags = noteFlags;
            n.highlights = std::move(highlights);
            return;
        }
    }
    if (notes.size() < kMaxNotes) {
        notes.push_back(ReplayNote{
            static_cast<uint16_t>(std::min<size_t>(moveIndex, 65535u)),
            noteFlags,
            std::move(text),
            std::move(highlights)
        });
    }
}

void ReplayTrack::addHighlight(size_t moveIndex, ReplayHighlight highlight) {
    for (auto& n : notes) {
        if (static_cast<size_t>(n.moveIndex) == moveIndex) {
            if (n.highlights.size() < kMaxHighlightsPerNote) {
                n.highlights.push_back(highlight);
                n.flags |= ReplayNote_HasHighlights;
            }
            return;
        }
    }
}

ReplayTrack ReplayCodec::extractFromBoard(
    const SolitaireBoard& board,
    const std::string& gameId,
    const std::string& title
) {
    ReplayTrack track{};
    track.version = ReplayTrack::kFormatVersion;
    track.flags = ReplayTrack_None;
    track.gameId = gameId.substr(0, ReplayTrack::kMaxGameIdLen);
    track.seed = board.seed();
    track.title = sanitizePlainText(title, ReplayTrack::kMaxTitleLen);

    SolitaireBoard boardCopy = board;
    std::vector<SolverMove> reversed;

    while (!boardCopy.txUndoStack().empty() || !boardCopy.undoStack().empty()) {
        if (!boardCopy.txUndoStack().empty()) {
            const TurnTransaction tx = boardCopy.txUndoStack().back();
            if (!boardCopy.undo()) break;

            SolverMove mv{};
            mv.action = SolverMove::Action::MoveCards;
            bool mapped = false;
            for (const auto& op : tx.ops) {
                if (op.type == BoardOpType::MoveCards) {
                    mv.fromPile = op.fromPile;
                    mv.toPile = op.toPile;
                    mv.cardIndex = static_cast<uint8_t>(std::min<uint32_t>(op.cardIdx, 127u));
                    mapped = true;
                    break;
                }
            }
            if (!mapped) {
                mv.action = SolverMove::Action::DrawStock;
            }
            mv.notation = buildMoveNotation(boardCopy, mv);
            reversed.push_back(std::move(mv));
        } else {
            const MoveCommand cmd = boardCopy.undoStack().back();
            if (!boardCopy.undo()) break;

            SolverMove mv{};
            const auto& piles = boardCopy.piles();
            if (cmd.spiderStockDeal || cmd.recycledWaste ||
                (cmd.fromPile == 0 && cmd.toPile == 1 && !piles.empty() && piles[0].type == PileType::Stock)) {
                mv.action = SolverMove::Action::DrawStock;
                mv.fromPile = cmd.fromPile;
                mv.toPile = cmd.toPile;
            } else if (cmd.isPairMove && cmd.secondFromPile < piles.size()) {
                mv.action = SolverMove::Action::PairPyramid;
                mv.fromPile = cmd.fromPile;
                mv.secondFromPile = cmd.secondFromPile;
                mv.toPile = cmd.toPile;
                if (!piles[cmd.fromPile].cards.empty()) {
                    mv.cardIndex = static_cast<uint8_t>(std::min<size_t>(piles[cmd.fromPile].cards.size() - 1, 127u));
                }
            } else {
                mv.action = SolverMove::Action::MoveCards;
                mv.fromPile = cmd.fromPile;
                mv.toPile = cmd.toPile;
                if (cmd.fromPile < piles.size() && piles[cmd.fromPile].cards.size() >= cmd.cardCount) {
                    mv.cardIndex = static_cast<uint8_t>(
                        std::min<size_t>(piles[cmd.fromPile].cards.size() - cmd.cardCount, 127u)
                    );
                }
            }
            mv.notation = buildMoveNotation(boardCopy, mv);
            reversed.push_back(std::move(mv));
        }
    }

    track.moves.reserve(std::min(reversed.size(), ReplayTrack::kMaxMoves));
    for (auto it = reversed.rbegin(); it != reversed.rend() && track.moves.size() < ReplayTrack::kMaxMoves; ++it) {
        track.moves.push_back(std::move(*it));
    }
    return track;
}

std::string ReplayCodec::encode(const ReplayTrack& track) {
    BitWriter w;

    const std::string gid = track.gameId.substr(0, ReplayTrack::kMaxGameIdLen);
    const std::string title = sanitizePlainText(track.title, ReplayTrack::kMaxTitleLen);
    const size_t moveCount = std::min(track.moves.size(), ReplayTrack::kMaxMoves);

    // Header
    w.writeU8(ReplayTrack::kFormatVersion);
    w.writeU8(track.flags);
    w.writeU32(track.seed);
    w.writeU8(static_cast<uint8_t>(gid.size()));
    w.writeBytes(gid);
    w.writeU8(static_cast<uint8_t>(title.size()));
    w.writeBytes(title);
    w.writeU16(static_cast<uint16_t>(moveCount));

    // Bit-packed move stream
    for (size_t i = 0; i < moveCount; ++i) {
        const auto& m = track.moves[i];
        uint32_t actCode = 0u;
        switch (m.action) {
            case SolverMove::Action::MoveCards:   actCode = 0u; break;
            case SolverMove::Action::DrawStock:   actCode = 1u; break;
            case SolverMove::Action::ClickCard:   actCode = 2u; break;
            case SolverMove::Action::PairPyramid: actCode = 3u; break;
            default:                              actCode = 0u; break;
        }
        w.writeBits(actCode, 2);
        if (actCode == 0u) {
            w.writeBits(m.fromPile & 0x3Fu, 6);
            w.writeBits(m.toPile & 0x3Fu, 6);
            w.writeBits(m.cardIndex & 0x7Fu, 7);
        } else if (actCode == 2u) {
            w.writeBits(m.fromPile & 0x3Fu, 6);
            w.writeBits(m.cardIndex & 0x7Fu, 7);
        } else if (actCode == 3u) {
            w.writeBits(m.fromPile & 0x3Fu, 6);
            w.writeBits(m.secondFromPile & 0x3Fu, 6);
            w.writeBits(m.toPile & 0x3Fu, 6);
        }
        // actCode == 1 (DrawStock) requires 0 extra bits
    }

    // Sparse commentary notes table
    std::vector<ReplayNote> validNotes;
    validNotes.reserve(std::min(track.notes.size(), ReplayTrack::kMaxNotes));
    for (const auto& n : track.notes) {
        if (validNotes.size() >= ReplayTrack::kMaxNotes) break;
        std::string clean = sanitizePlainText(n.text, ReplayTrack::kMaxNoteTextLen);
        if (!clean.empty()) {
            ReplayNote copy{};
            copy.moveIndex = n.moveIndex;
            copy.flags = n.flags;
            copy.text = std::move(clean);
            const size_t hlCount = std::min(n.highlights.size(), ReplayTrack::kMaxHighlightsPerNote);
            copy.highlights.assign(n.highlights.begin(), n.highlights.begin() + static_cast<ptrdiff_t>(hlCount));
            if (!copy.highlights.empty()) {
                copy.flags |= ReplayNote_HasHighlights;
            } else {
                copy.flags &= static_cast<uint8_t>(~ReplayNote_HasHighlights);
            }
            validNotes.push_back(std::move(copy));
        }
    }

    w.writeU16(static_cast<uint16_t>(validNotes.size()));
    for (const auto& n : validNotes) {
        w.writeU16(n.moveIndex);
        w.writeU8(n.flags);
        w.writeU16(static_cast<uint16_t>(n.text.size()));
        w.writeBytes(n.text);
        if ((n.flags & ReplayNote_HasHighlights) != 0) {
            w.writeU8(static_cast<uint8_t>(n.highlights.size()));
            for (const auto& h : n.highlights) {
                const uint8_t meta = static_cast<uint8_t>(
                    (static_cast<uint8_t>(h.kind) & 0x03u) |
                    ((static_cast<uint8_t>(h.style) & 0x03u) << 2)
                );
                w.writeU8(meta);
                w.writeU8(h.pileIdx);
                w.writeU8(h.startCard);
                w.writeU8(h.count);
            }
        }
    }

    return "sol1:" + toUrlSafeBase64(w.bytes());
}

bool ReplayCodec::decode(std::string_view code, ReplayTrack& outTrack, std::string* outError) {
    auto fail = [&](const char* msg) -> bool {
        if (outError) *outError = msg;
        return false;
    };

    const std::string_view payload = extractPayload(code);
    if (payload.empty()) {
        return fail("Missing 'sol1:' replay prefix");
    }

    const std::vector<uint8_t> raw = eatsbits::ui::base64Decode(payload);
    if (raw.size() < 10) {
        return fail("Replay payload too short");
    }

    BitReader r(raw);
    ReplayTrack t{};

    uint8_t gidLen = 0, titleLen = 0;
    uint16_t moveCount = 0;
    if (!r.readU8(t.version)) return fail("Corrupt replay header");
    if (t.version != ReplayTrack::kFormatVersion) return fail("Unsupported replay version");
    if (!r.readU8(t.flags)) return fail("Corrupt replay flags");
    if (!r.readU32(t.seed)) return fail("Corrupt replay seed");
    if (!r.readU8(gidLen) || gidLen > ReplayTrack::kMaxGameIdLen) return fail("Invalid gameId length");
    if (!r.readString(gidLen, t.gameId)) return fail("Truncated gameId");
    if (!r.readU8(titleLen) || titleLen > ReplayTrack::kMaxTitleLen) return fail("Invalid title length");
    if (!r.readString(titleLen, t.title)) return fail("Truncated title");
    t.title = sanitizePlainText(t.title, ReplayTrack::kMaxTitleLen);
    if (!r.readU16(moveCount) || moveCount > ReplayTrack::kMaxMoves) return fail("Invalid move count");

    t.moves.reserve(moveCount);
    for (uint16_t i = 0; i < moveCount; ++i) {
        uint32_t actCode = 0;
        if (!r.readBits(2, actCode)) return fail("Truncated move stream");

        SolverMove mv{};
        if (actCode == 0u) {
            uint32_t fp = 0, tp = 0, ci = 0;
            if (!r.readBits(6, fp) || !r.readBits(6, tp) || !r.readBits(7, ci)) {
                return fail("Truncated MoveCards record");
            }
            mv.action = SolverMove::Action::MoveCards;
            mv.fromPile = static_cast<uint8_t>(fp);
            mv.toPile = static_cast<uint8_t>(tp);
            mv.cardIndex = static_cast<uint8_t>(ci);
        } else if (actCode == 1u) {
            mv.action = SolverMove::Action::DrawStock;
            mv.fromPile = 0;
            mv.toPile = 1;
        } else if (actCode == 2u) {
            uint32_t fp = 0, ci = 0;
            if (!r.readBits(6, fp) || !r.readBits(7, ci)) {
                return fail("Truncated ClickCard record");
            }
            mv.action = SolverMove::Action::ClickCard;
            mv.fromPile = static_cast<uint8_t>(fp);
            mv.cardIndex = static_cast<uint8_t>(ci);
        } else {
            uint32_t fp = 0, sfp = 0, tp = 0;
            if (!r.readBits(6, fp) || !r.readBits(6, sfp) || !r.readBits(6, tp)) {
                return fail("Truncated PairPyramid record");
            }
            mv.action = SolverMove::Action::PairPyramid;
            mv.fromPile = static_cast<uint8_t>(fp);
            mv.secondFromPile = static_cast<uint8_t>(sfp);
            mv.toPile = static_cast<uint8_t>(tp);
        }
        t.moves.push_back(std::move(mv));
    }

    uint16_t noteCount = 0;
    if (!r.readU16(noteCount) || noteCount > ReplayTrack::kMaxNotes) {
        return fail("Invalid commentary note count");
    }

    t.notes.reserve(noteCount);
    for (uint16_t i = 0; i < noteCount; ++i) {
        ReplayNote n{};
        uint16_t textLen = 0;
        if (!r.readU16(n.moveIndex) || !r.readU8(n.flags) || !r.readU16(textLen)) {
            return fail("Truncated commentary header");
        }
        if (textLen > ReplayTrack::kMaxNoteTextLen) {
            return fail("Commentary note exceeds max length");
        }
        if (!r.readString(textLen, n.text)) {
            return fail("Truncated commentary text");
        }
        if ((n.flags & ReplayNote_HasHighlights) != 0) {
            uint8_t hlCount = 0;
            if (!r.readU8(hlCount) || hlCount > ReplayTrack::kMaxHighlightsPerNote) {
                return fail("Invalid highlight count");
            }
            n.highlights.reserve(hlCount);
            for (uint8_t hi = 0; hi < hlCount; ++hi) {
                uint8_t meta = 0, pIdx = 0, sCard = 0, cnt = 0;
                if (!r.readU8(meta) || !r.readU8(pIdx) || !r.readU8(sCard) || !r.readU8(cnt)) {
                    return fail("Truncated highlight record");
                }
                ReplayHighlight h{};
                h.kind = static_cast<HighlightKind>(meta & 0x03u);
                h.style = static_cast<HighlightStyle>((meta >> 2) & 0x03u);
                h.pileIdx = pIdx;
                h.startCard = sCard;
                h.count = cnt;
                n.highlights.push_back(h);
            }
        }
        n.text = sanitizePlainText(n.text, ReplayTrack::kMaxNoteTextLen);
        if (!n.text.empty()) {
            t.notes.push_back(std::move(n));
        }
    }

    outTrack = std::move(t);
    return true;
}

bool ReplayCodec::looksLikeReplayCode(std::string_view text) noexcept {
    return !extractPayload(text).empty();
}

} // namespace solengine
