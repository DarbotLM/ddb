"""DDB txt2kg — connector for the NVIDIA txt2kg knowledge graph builder.

Bridges DDB adaptive cards and micro DBs to the txt2kg pipeline
running on darbot@10.1.8.69 (spark-804f).
"""

__all__ = [
    "Txt2KGClient",
    "Txt2KGConfig",
    "Triple",
]

from txt2kg.client import Txt2KGClient, Txt2KGConfig
from txt2kg.models import Triple
