/**
 * Copyright      2023  Xiaomi Corporation (authors: Wei Kang)
 *
 * See LICENSE for clarification regarding multiple authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sherpa/csrc/context-graph.h"

#include <cassert>
#include <queue>
#include <utility>

namespace sherpa {
void ContextGraph::Build(
    const std::vector<std::vector<int32_t>> &token_ids) const {
  for (int32_t i = 0; i < static_cast<int32_t>(token_ids.size()); ++i) {
    auto node = root_.get();
    for (int32_t j = 0; j < static_cast<int32_t>(token_ids[i].size()); ++j) {
      int32_t token = token_ids[i][j];
      if (0 == node->next.count(token)) {
        bool is_end = j == (static_cast<int32_t>(token_ids[i].size()) - 1);
        node->next[token] = std::make_unique<ContextState>(
            token, context_score_, node->node_score + context_score_,
            is_end ? 0 : node->local_node_score + context_score_, is_end);
      }
      node = node->next[token].get();
    }
  }
  FillFailOutput();
}

std::pair<float, const ContextState *> ContextGraph::ForwardOneStep(
    const ContextState *state, int32_t token) const {
  const ContextState *node;
  float score;
  if (1 == state->next.count(token)) {
    node = state->next.at(token).get();
    score = node->token_score;
    if (state->is_end) score += state->node_score;
  } else {
    node = state->fail;
    while (0 == node->next.count(token)) {
      node = node->fail;
      if (-1 == node->token) break;  // root
    }
    if (1 == node->next.count(token)) {
      node = node->next.at(token).get();
    }
    score = node->node_score - state->local_node_score;
  }
  SHERPA_CHECK(nullptr != node);
  float matched_score = 0;
  auto output = node->output;
  while (nullptr != output) {
    matched_score += output->node_score;
    output = output->output;
  }
  return std::make_pair(score + matched_score, node);
}

std::pair<float, const ContextState *> ContextGraph::Finalize(
    const ContextState *state) const {
  float score = -state->node_score;
  if (state->is_end) {
    score = 0;
  }
  return std::make_pair(score, root_.get());
}

void ContextGraph::FillFailOutput() const {
  std::queue<const ContextState *> node_queue;
  for (auto &kv : root_->next) {
    kv.second->fail = root_.get();
    node_queue.push(kv.second.get());
  }
  while (!node_queue.empty()) {
    auto current_node = node_queue.front();
    node_queue.pop();
    for (auto &kv : current_node->next) {
      auto fail = current_node->fail;
      if (1 == fail->next.count(kv.first)) {
        fail = fail->next.at(kv.first).get();
      } else {
        fail = fail->fail;
        while (0 == fail->next.count(kv.first)) {
          fail = fail->fail;
          if (-1 == fail->token) break;
        }
        if (1 == fail->next.count(kv.first))
          fail = fail->next.at(kv.first).get();
      }
      kv.second->fail = fail;
      // fill the output arc
      auto output = fail;
      while (!output->is_end) {
        output = output->fail;
        if (-1 == output->token) {
          output = nullptr;
          break;
        }
      }
      kv.second->output = output;
      node_queue.push(kv.second.get());
    }
  }
}
}  // namespace sherpa
