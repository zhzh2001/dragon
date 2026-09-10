"""SDPA-backed stand-in for FlashAttention 3, for hardware FlashAttention cannot serve.

ComfyUI-SkinTokens (and the SkinTokens model it wraps) imports
`flash_attn_interface.flash_attn_func` first, and falls back to
`flash_attn.flash_attn_interface`. Both are FlashAttention, and FA2 requires
sm_80 or newer; t5810 is a 2080 Ti at sm_75, so neither can run there. Upstream
has no SDPA path at all -- the import is unguarded.

Providing this module satisfies that first import, so no upstream file is
patched. PyTorch scaled_dot_product_attention computes the same attention,
without the IO-aware kernel: slower and heavier on memory, which is the trade
for running at all on Turing.

Layout is the only real difference. FlashAttention takes
(batch, seqlen, heads, head_dim); SDPA takes (batch, heads, seqlen, head_dim).
FA3 also returns (out, lse) and the callers unpack two values, so this does too.
"""
import torch
import torch.nn.functional as F

__all__ = ["flash_attn_func"]


def flash_attn_func(q, k, v, dropout_p=0.0, softmax_scale=None, causal=False,
                    *args, **kwargs):
    q, k, v = (t.transpose(1, 2) for t in (q, k, v))
    out = F.scaled_dot_product_attention(
        q, k, v, dropout_p=dropout_p, is_causal=causal, scale=softmax_scale)
    # lse is returned as None; no caller in this codebase consumes it.
    return out.transpose(1, 2), None
