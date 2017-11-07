/**
* This file is part of ibow-lcd.
*
* Copyright (C) 2017 Emilio Garcia-Fidalgo <emilio.garcia@uib.es> (University of the Balearic Islands)
*
* ibow-lcd is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ibow-lcd is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ibow-lcd. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ibow-lcd/lcdetector.h"

namespace ibow_lcd {

LCDetector::LCDetector(const LCDetectorParams& params) :
      last_lc_island_(-1, 0.0, -1, -1) {
  // Creating the image index
  index_ = std::make_shared<obindex2::ImageIndex>(params.k,
                                                  params.s,
                                                  params.t,
                                                  params.merge_policy,
                                                  params.purge_descriptors,
                                                  params.min_feat_apps);
  // Storing the remaining parameters
  p_ = params.p;
  nndr_ = params.nndr;
  min_score_ = params.min_score;
  island_size_ = params.island_size;
  island_offset_ = island_size_ / 2;
  last_lc_result_.status = LC_NOT_DETECTED;
}

LCDetector::~LCDetector() {}

void LCDetector::process(const unsigned image_id,
                         const std::vector<cv::KeyPoint>& kps,
                         const cv::Mat& descs,
                         LCDetectorResult* result) {
  // Adding the current image to the queue to be added in the future
  queue_ids_.push(image_id);
  queue_kps_.push(kps);
  queue_descs_.push(descs);

  // Assessing if, at least, p images have arrived
  if (queue_ids_.size() < p_) {
    result->status = LC_NOT_ENOUGH_IMAGES;
    last_lc_result_.status = LC_NOT_ENOUGH_IMAGES;
    return;
  }

  // Adding new hypothesis
  unsigned newimg_id = queue_ids_.front();
  queue_ids_.pop();

  std::vector<cv::KeyPoint> newimg_kps = queue_kps_.front();
  queue_kps_.pop();

  cv::Mat newimg_descs = queue_descs_.front();
  queue_descs_.pop();

  addImage(newimg_id, newimg_kps, newimg_descs);

  // Searching similar images in the index
  // Matching the descriptors agains the current visual words
  std::vector<std::vector<cv::DMatch> > matches_feats;

  // Searching the query descriptors against the features
  index_->searchDescriptors(descs, &matches_feats, 2, 64);

  // Filtering matches according to the ratio test
  std::vector<cv::DMatch> matches;
  filterMatches(matches_feats, &matches);

  std::vector<obindex2::ImageMatch> image_matches;

  // We look for similar images according to the filtered matches found
  index_->searchImages(descs, matches, &image_matches, true);

  // Filtering the resulting image matchings
  std::vector<obindex2::ImageMatch> image_matches_filt;
  filterCandidates(image_matches, &image_matches_filt);

  std::vector<Island> islands;
  buildIslands(image_matches_filt, &islands);

  // std::cout << "Resulting Islands:" << std::endl;
  // for (unsigned i = 0; i < islands.size(); i++) {
  //   std::cout << islands[i].toString();
  // }

  if (!islands.size()) {
    // No resulting islands
    result->status = LC_NOT_ENOUGH_ISLANDS;
    last_lc_result_.status = LC_NOT_ENOUGH_ISLANDS;
    return;
  }

  // We process the resulting islands according to the previous detections
  if (last_lc_result_.status != LC_DETECTED &&
      last_lc_result_.status != LC_TRANSITION) {
    // We get the best island and try to close a loop with the best image inside
    Island best_island = islands[0];
    unsigned best_img = best_island.img_id;

    // We obtain the image matchings, since we need them for compute F
    std::unordered_map<unsigned, obindex2::PointMatches> point_matches;
    index_->getMatchings(kps, matches, &point_matches);
    obindex2::PointMatches p_matches = point_matches[best_img];

    unsigned inliers = checkEpipolarGeometry(p_matches.query, p_matches.train);

    if (inliers > 12) {
      // LOOP detected
      result->status = LC_DETECTED;
      result->query_id = image_id;
      result->train_id = best_img;
      result->inliers = inliers;
      last_lc_island_ = best_island;
      // Store the last result
      last_lc_result_ = *result;
    } else {
      result->status = LC_NOT_ENOUGH_INLIERS;
      last_lc_result_.status = LC_NOT_ENOUGH_INLIERS;
    }
  } else {
    // We search for islands similar to the previous one
    std::vector<Island> p_islands;
    getPriorIslands(last_lc_island_, islands, &p_islands);

    // We obtain the image matchings, since we need them for compute F
    std::unordered_map<unsigned, obindex2::PointMatches> point_matches;
    index_->getMatchings(kps, matches, &point_matches);

    // We validate the epipolar geometry against each prior island
    bool found = false;
    for (unsigned i = 0; i < p_islands.size(); i++) {
      Island island = p_islands[i];
      unsigned best_img = island.img_id;

      // Getting the corresponding matchings
      obindex2::PointMatches p_matches = point_matches[best_img];

      unsigned inliers = checkEpipolarGeometry(p_matches.query,
                                               p_matches.train);

      if (inliers > 12) {
        // LOOP detected
        result->status = LC_DETECTED;
        result->query_id = image_id;
        result->train_id = best_img;
        result->inliers = inliers;
        last_lc_island_ = island;
        found = true;
        // Store the last result
        last_lc_result_ = *result;
        break;
      }
    }

    if (!found) {
      if (image_id - last_lc_result_.query_id > 5) {
        result->status = LC_NOT_DETECTED;
        last_lc_result_.status = LC_NOT_DETECTED;
      } else {
        result->status = LC_TRANSITION;
        last_lc_result_.status = LC_TRANSITION;
      }
    }
  }
}

void LCDetector::addImage(const unsigned image_id,
                          const std::vector<cv::KeyPoint>& kps,
                          const cv::Mat& descs) {
  if (index_->numImages() == 0) {
    // This is the first image that is inserted into the index
    index_->addImage(image_id, kps, descs);
  } else {
    // We have to search the descriptor and filter them before adding descs
    // Matching the descriptors
    std::vector<std::vector<cv::DMatch> > matches_feats;

    // Searching the query descriptors against the features
    index_->searchDescriptors(descs, &matches_feats, 2, 64);

    // Filtering matches according to the ratio test
    std::vector<cv::DMatch> matches;
    filterMatches(matches_feats, &matches);

    // Finally, we add the image taking into account the correct matchings
    index_->addImage(image_id, kps, descs, matches);
  }
}

void LCDetector::filterMatches(
      const std::vector<std::vector<cv::DMatch> >& matches_feats,
      std::vector<cv::DMatch>* matches) {
  // Clearing the current matches vector
  matches->clear();

  // Filtering matches according to the ratio test
  for (unsigned m = 0; m < matches_feats.size(); m++) {
    if (matches_feats[m][0].distance <= matches_feats[m][1].distance * nndr_) {
      matches->push_back(matches_feats[m][0]);
    }
  }
}

void LCDetector::filterCandidates(
      const std::vector<obindex2::ImageMatch>& image_matches,
      std::vector<obindex2::ImageMatch>* image_matches_filt) {
  image_matches_filt->clear();

  double max_score = image_matches[0].score;
  double min_score = image_matches[image_matches.size() - 1].score;

  for (unsigned i = 0; i < image_matches.size(); i++) {
    // Computing the new score
    double new_score = (image_matches[i].score - min_score) /
                       (max_score - min_score);
    // Assessing if this image match is higher than a threshold
    if (new_score > min_score_) {
      obindex2::ImageMatch match = image_matches[i];
      match.score = new_score;
      image_matches_filt->push_back(match);
    } else {
      break;
    }
  }
}

void LCDetector::buildIslands(
      const std::vector<obindex2::ImageMatch>& image_matches,
      std::vector<Island>* islands) {
  islands->clear();

  // We process each of the resulting image matchings
  for (unsigned i = 0; i < image_matches.size(); i++) {
    // Getting information about this match
    unsigned curr_img_id = static_cast<unsigned>(image_matches[i].image_id);
    double curr_score = image_matches[i].score;

    // Theoretical island limits
    unsigned min_id = static_cast<unsigned>
                              (std::max((int)curr_img_id - (int)island_offset_,
                               0));
    unsigned max_id = curr_img_id + island_offset_;

    // We search for the closest island
    bool found = false;
    for (unsigned j = 0; j < islands->size(); j++) {
      if (islands->at(j).fits(curr_img_id)) {
        islands->at(j).incrementScore(curr_score);
        found = true;
        break;
      } else {
        // We adjust the limits of a future island
        islands->at(j).adjustLimits(curr_img_id, &min_id, &max_id);
      }
    }

    // Creating a new island if required
    if (!found) {
      Island new_island(curr_img_id,
                        curr_score,
                        min_id,
                        max_id);
      islands->push_back(new_island);
    }
  }

  // Normalizing the final scores according to the number of images
  for (unsigned j = 0; j < islands->size(); j++) {
    islands->at(j).normalizeScore();
  }

  std::sort(islands->begin(), islands->end());
}

void LCDetector::getPriorIslands(
      const Island& island,
      const std::vector<Island>& islands,
      std::vector<Island>* p_islands) {
  p_islands->clear();

  // We search for overlapping islands
  for (unsigned i = 0; i < islands.size(); i++) {
    Island tisl = islands[i];
    if (island.overlaps(tisl)) {
      p_islands->push_back(tisl);
    }
  }
}

unsigned LCDetector::checkEpipolarGeometry(
                                      const std::vector<cv::Point2f>& query,
                                      const std::vector<cv::Point2f>& train) {
  std::vector<uchar> inliers(query.size(), 0);
  if (query.size() > 7) {
    cv::Mat F =
      cv::findFundamentalMat(
        cv::Mat(query), cv::Mat(train),      // Matching points
        CV_FM_RANSAC,                        // RANSAC method
        3.0,                                 // Distance to epipolar line
        0.985,                               // Confidence probability
        inliers);                            // Match status (inlier or outlier)
  }

  // Extract the surviving (inliers) matches
  auto it = inliers.begin();
  unsigned total_inliers = 0;
  for (; it != inliers.end(); it++) {
    if (*it)
      total_inliers++;
  }

  return total_inliers;
}

}  // namespace ibow_lcd
