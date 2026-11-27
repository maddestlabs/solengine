# SolEngine Game Library — Master Bibliography

Every historical claim, rule convention, and solvability figure in [docs/games/](README.md) must cite a key from this bibliography.

## Citation Policy

1. **Primary & Authoritative First:** Prefer original print compendiums (19th/20th-century patience books), peer-reviewed academic papers, and recognized card-game historians (e.g., David Parlett, Albert Morehead, Geoffrey Mott-Smith).
2. **Separate Fact from Folklore:** Popular origin stories (gold rushes, Napoleon in exile, casino anecdotes) must be backed by print evidence or explicitly labeled **Unverified / Folklore**.
3. **Version & Edition Precision:** Rules drift across editions (especially in *Dick's* and *Hoyle*). Always cite the edition or year when a rule or name changed.
4. **Distinguish Solvability Models:** Always specify whether a cited win rate is for **thoughtful** (perfect-information / omniscient solver) play, **hidden-information AI** play, or **human** play, and state the exact rule variant (e.g., Draw 1 vs. Draw 3, unlimited vs. limited redeals, worrying back allowed or not).

---

## 1. Historical Patience & Solitaire Compendiums (19th – Mid-20th Century)

- **`[Cadogan1870]`** — Cadogan, Lady Adelaide. *Illustrated Games of Patience*. London: Sampson Low, Marston, Low, & Searle, c. 1870 (multiple expanded editions through the 1880s). One of the earliest influential English-language patience anthologies.
- **`[Dick1883]`** — Dick, William Brisbane (ed.). *Dick's Games of Patience, or Solitaire with Cards*. New York: Dick & Fitzgerald, 1883. Landmark American patience compendium; later expanded editions (e.g., 1908/1912) reflect early 20th-century American naming shifts.
- **`[Jones1888]`** — Whitmore Jones, Mary. *Games of Patience for One or More Players*. London: L. Upcott Gill, 1888 (published across multiple series, 1888–1900).
- **`[Dick1898]`** — Dick, Harris B. (ed.). *Dick's Games of Patience, or Solitaire with Cards — Second Series*. New York: Dick & Fitzgerald, 1898.
- **`[Hoyle1907]`** — *Hoyle's Games: Autograph Edition*. New York: McClure, Phillips & Co., 1907. Notable early print documentation of "Seven-Card Klondike" alongside the 13-card reserve game then titled "Klondike" (later standardized as Canfield).
- **`[Jones1911]`** — Whitmore Jones, Mary. *New Games of Patience*. London: L. Upcott Gill, 1911. Contains the earliest print appearance of *Westcliff* (the 21-card 7×3 ancestor of *Easthaven*).
- **`[USPC1913]`** — United States Playing Card Company. *The Official Rules of Card Games: Hoyle Up-to-Date*. Cincinnati: U.S. Playing Card Co., 1913. Key edition stabilizing the modern American distinction between *Klondike* (7-column tableau) and *Canfield* (13-card reserve).
- **`[MoreheadMottSmith1949]`** — Morehead, Albert H., and Geoffrey Mott-Smith. *The Complete Book of Solitaire and Patience Games*. New York: Longmans, Green & Co., 1949 (later Bantam/Foulsham reprints). Foundational 20th-century American reference cataloging 225+ games with rules, terminology, and odds estimates.

---

## 2. Modern Authoritative Reference Works

- **`[Gardner1968]`** — Gardner, Martin. *"Mathematical Games: Combinatorial Problems, Some Old, Some New and All Newly Attacked by Computer."* *Scientific American*, Vol. 218, No. 6, June 1968, pp. 112–118. First print publication of *Baker's Game* (credited to C. L. Baker), which directly inspired Paul Alfille's *FreeCell* (1978).
- **`[Parlett1979]`** — Parlett, David. *The Penguin Book of Patience*. London: Allen Lane / Penguin Books, 1979 (also published as *The Penguin Encyclopedia of Card Games* and expanded online at `parlettgames.uk`). Standard historical and taxonomic reference by card-game historian David Parlett.
- **`[ParlettSite]`** — Parlett, David. *Historic Card Games / Patience & Solitaire*. Available at: https://www.parlettgames.uk/patience/ (ongoing archival reference by the author of `[Parlett1979]`).
- **`[KellerSolLab]`** — Keller, Michael. *Solitaire Laboratory*. Available at: https://www.solitairelaboratory.com/ — Authoritative historical detective work, rule-propagation audits, lineage essays, and FreeCell/patience research.
- **`[KellerWorstSolitaire]`** — Keller, Michael. *"The Worst Solitaire Mistake? — Auld Lang Syne."* *Solitaire Laboratory*. Available at: https://www.solitairelaboratory.com/worstsolitaire.html — Classic case study on how rulebook transcription errors corrupt patience games across editions.

---

## 3. Academic & Computational Solver Literature

- **`[Helmert2003]`** — Helmert, Malte. *"Complexity results for standard benchmark domains in planning."* *Artificial Intelligence*, Vol. 143, No. 2, pp. 219–262, 2003. Proves that generalized FreeCell (for arbitrary deck sizes) is NP-complete.
- **`[Bjarnason2007]`** — Bjarnason, Ronald, Prasad Tadepalli, and Alan Fern. *"Searching Solitaire in Real Time."* *ICGA Journal*, Vol. 30, No. 3, pp. 131–142, 2007. Early real-time search and heuristic planning study on Klondike Solitaire.
- **`[Bjarnason2009]`** — Bjarnason, Ronald, Alan Fern, and Prasad Tadepalli. *"Lower Bounding Klondike Solitaire with Monte-Carlo Planning."* Proceedings of the 19th International Conference on Automated Planning and Scheduling (ICAPS), pp. 26–33, 2009. Established strong lower bounds on both thoughtful and standard (hidden-information) Klondike win rates.
- **`[BlakeGent2019]`** — Blake, Charlie, and Ian P. Gent. *"The Winnability of Klondike Solitaire and Many Other Patience Games."* arXiv:1906.12314 [cs.AI], June 2019. Available at: https://arxiv.org/abs/1906.12314. Introduced the general-purpose DFS solver *Solvitaire* with transposition tables, symmetry breaking, and dominance rules; established tight confidence intervals for Thoughtful Klondike (Draw 1: 90.480% ± 0.116%; Draw 3: 81.945% ± 0.084%) and many other patience variants.
- **`[WolterSolitaire]`** — Wolter, Jan. *Experimental Analysis of Klondike Solitaire and Other Patience Games* (archive of `politaire.com` / `unixpapa.com` solver benchmarks). Influential empirical solver studies across Klondike, Spider, FreeCell, and Canfield families.
- **`[KellerFreeCell]`** — Keller, Michael. *FreeCell FAQ and Unsolvable Deals*. *Solitaire Laboratory*. Available at: https://www.solitairelaboratory.com/fcfaq.html — Authoritative reference for Paul Alfille's 1978 PLATO origin, Microsoft FreeCell deal numbering, unsolvable deals (e.g., #11982), and *Baker's Game* / *Eight Off* lineage.

---

## 4. Recognized Reference Software & Compendiums

SolEngine treats established commercial and open-source solitaire collections as authoritative implementation references and cross-links to them in [catalog.json](catalog.json):

- **`[GoodsolPGS]`** — Warfield, Thomas (Goodsol Development). *Pretty Good Solitaire*. Available at: https://www.goodsol.com/ — One of the most comprehensive and historically grounded commercial solitaire collections (1,000+ games) with detailed online rules documentation.
- **`[SolitaireForever2]`** — *Solitaire Forever II*. Available at: https://www.solitaireforever.com/ — Extensive commercial solitaire engine and rule reference.
- **`[BVSSolitaire]`** — BVS Development Corporation. *BVS Solitaire Collection*. Available at: https://www.bvssolitaire.com/ — Long-running commercial solitaire collection (500+ variants) with rule comparisons and statistics.
- **`[PySolFC]`** — PySolFC Contributors. *PySol Fan Club Edition*. Available at: https://pysolfc.sourceforge.io/ — Landmark open-source solitaire collection (1,000+ games, including non-standard decks and regional variants).

---

## 5. Adding New References

When documenting a new game:
1. Add the full citation under the appropriate section above using the key format `[AuthorYear]` (or `[ShortOrgYear]`).
2. Keep entries sorted chronologically within each section.
3. Verify publication year, author, and exact claim before committing.
