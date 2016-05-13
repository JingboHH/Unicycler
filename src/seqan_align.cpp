#include "seqan_align.h"

#include <seqan/align.h>
#include <iostream>
#include <vector>
#include "settings.h"
#include <limits>
#include <algorithm>



// This is the big function called by Python code. It conducts a semi-global Seqan alignment
// the given read against all references and returns the console output and all found alignments in a
// string.
char * semiGlobalAlignmentAllRefs(char * readNameC, char * readSeqC, int verbosity,
                                  double expectedSlope, KmerPositions * kmerPositions,
                                  int matchScore, int mismatchScore, int gapOpenScore, int gapExtensionScore,
                                  double lowScoreThreshold) {
    // This string will collect all of the console output for the alignment.
    std::string output;

    // Change the read name and sequence to C++ strings.
    std::string readName(readNameC);
    std::string posReadName = readName + "+";
    std::string negReadName = readName + "-";
    std::string posReadSeq(readSeqC);
    std::string negReadSeq = getReverseComplement(posReadSeq);
    int readLength = posReadSeq.length();

    // At this point, the kmerPositions object should have only the reference sequences.
    std::vector<std::string> referenceNames = kmerPositions->getAllNames();

    // Add both the forward and reverse read sequences to the KmerPositions object.
    kmerPositions->addPositions(posReadName, posReadSeq);
    kmerPositions->addPositions(negReadName, negReadSeq);

    // Create a CommonKmerSet for the read (both forward and reverse complement) and every reference.
    std::vector<CommonKmerSet *> commonKmerSets;
    float maxScoreAllSets = 0.0f;
    CommonKmerSet * commonKmerSet;
    for (size_t i = 0; i < referenceNames.size(); ++i) {
        std::string refName = referenceNames[i];
        int refLength = kmerPositions->getLength(refName);

        // Forward read sequence
        commonKmerSet = new CommonKmerSet(posReadName, refName, readLength, refLength, COMMON_KMER_BAND_SIZE, expectedSlope, kmerPositions);
        if (commonKmerSet->m_maxScore < MINIMUM_MAX_SCORE)
            delete commonKmerSet;
        else {
            commonKmerSets.push_back(commonKmerSet);
            maxScoreAllSets = std::max(maxScoreAllSets, commonKmerSet->m_maxScore);
        }

        // Reverse read sequence
        commonKmerSet = new CommonKmerSet(negReadName, refName, readLength, refLength, COMMON_KMER_BAND_SIZE, expectedSlope, kmerPositions);
        if (commonKmerSet->m_maxScore < MINIMUM_MAX_SCORE)
            delete commonKmerSet;
        else {
            commonKmerSets.push_back(commonKmerSet);
            maxScoreAllSets = std::max(maxScoreAllSets, commonKmerSet->m_maxScore);
        }
    }

    // Sort the common k-mer sets by their max score so high-scoring sets are used first.
    std::sort(commonKmerSets.begin(), commonKmerSets.end(), [](const CommonKmerSet * a, const CommonKmerSet * b) {
        return a->m_maxScore > b->m_maxScore;   
    });

    // Now for the alignments! We first try at sensitivity level 1.
    std::vector<SemiGlobalAlignment *> alignments = semiGlobalAlignmentAllRefsOneLevel(commonKmerSets, kmerPositions,
                                                                                       verbosity, output,
                                                                                       matchScore, mismatchScore, gapOpenScore, gapExtensionScore,
                                                                                       1, maxScoreAllSets);

    // Assess whether the read is covered by alignments of sufficient quality. If not, we try
    // sensititivity level 2.
    if (needsMoreSensitiveAlignment(alignments, lowScoreThreshold)) {
        std::vector<SemiGlobalAlignment *> l2Alignments = semiGlobalAlignmentAllRefsOneLevel(commonKmerSets, kmerPositions,
                                                                                             verbosity, output,
                                                                                             matchScore, mismatchScore, gapOpenScore, gapExtensionScore,
                                                                                             1, maxScoreAllSets);
        alignments.insert(alignments.end(), l2Alignments.begin(), l2Alignments.end());

        // If there are still completely unaligned parts of the read, we try sensitivity level 3.
        if (readHasUnalignedParts(alignments)) {
            std::vector<SemiGlobalAlignment *> l3Alignments = semiGlobalAlignmentAllRefsOneLevel(commonKmerSets, kmerPositions,
                                                                                                 verbosity, output,
                                                                                                 matchScore, mismatchScore, gapOpenScore, gapExtensionScore,
                                                                                                 1, maxScoreAllSets);
            alignments.insert(alignments.end(), l3Alignments.begin(), l3Alignments.end());   
        }
    }

    // Clean up.
    kmerPositions->deletePositions(readName);
    for (size_t i = 0; i < commonKmerSets.size(); ++i)
        delete commonKmerSets[i];

    // The returned string is semicolon-delimited. The last part is the console output and the
    // other parts are alignment description strings.
    std::string returnString;
    for (size_t i = 0; i < alignments.size(); ++i) {
        returnString += alignments[i]->getFullString() + ";";
        delete alignments[i];
    }
    returnString += output;
    return cppStringToCString(returnString);
}






std::vector<SemiGlobalAlignment *> semiGlobalAlignmentAllRefsOneLevel(std::vector<CommonKmerSet *> & commonKmerSets,
                                                                      KmerPositions * kmerPositions,
                                                                      int verbosity, std::string & output,
                                                                      int matchScore, int mismatchScore,
                                                                      int gapOpenScore, int gapExtensionScore,
                                                                      int sensitivityLevel, float maxScoreAllSets) {
    // Set the algorithm settings using the sentitivity level.
    double lowScoreThreshold, highScoreThreshold, mergeDistance, minAlignmentLength;
    int minPointCount;
    if (sensitivityLevel == 1) {
        lowScoreThreshold = LOW_SCORE_THRESHOLD_LEVEL_1;
        highScoreThreshold = HIGH_SCORE_THRESHOLD_LEVEL_1;
        mergeDistance = MERGE_DISTANCE_LEVEL_1;
        minAlignmentLength = MIN_ALIGNMENT_LENGTH_LEVEL_1;
        minPointCount = MIN_POINT_COUNT_LEVEL_1;
    }
    else if (sensitivityLevel == 2) {
        lowScoreThreshold = LOW_SCORE_THRESHOLD_LEVEL_2;
        highScoreThreshold = HIGH_SCORE_THRESHOLD_LEVEL_2;
        mergeDistance = MERGE_DISTANCE_LEVEL_2;
        minAlignmentLength = MIN_ALIGNMENT_LENGTH_LEVEL_2;
        minPointCount = MIN_POINT_COUNT_LEVEL_2;
    }
    else { // sensitivityLevel == 3
        lowScoreThreshold = LOW_SCORE_THRESHOLD_LEVEL_3;
        highScoreThreshold = HIGH_SCORE_THRESHOLD_LEVEL_3;
        mergeDistance = MERGE_DISTANCE_LEVEL_3;
        minAlignmentLength = MIN_ALIGNMENT_LENGTH_LEVEL_3;
        minPointCount = MIN_POINT_COUNT_LEVEL_3;
    }

    // The low and high score thresholds are initially expressed as a fraction of the max score.
    // Now turn them into absolute values.
    lowScoreThreshold *= maxScoreAllSets;
    highScoreThreshold *= maxScoreAllSets;

    Score<int, Simple> scoringScheme(matchScore, mismatchScore, gapExtensionScore, gapOpenScore);

    // Go through the common k-mer sets and perform line-finding and then aligning.
    std::vector<SemiGlobalAlignment *> alignments;
    for (size_t i = 0; i < commonKmerSets.size(); ++i) {
        CommonKmerSet * commonKmerSet = commonKmerSets[i];

        // If a common k-mer set's max score is below the high threshold, then we know there won't
        // be any alignment lines, so don't bother continuing.
        if (commonKmerSet->m_maxScore < highScoreThreshold)
            continue;

        std::string readName = commonKmerSet->m_readName;
        std::string refName = commonKmerSet->m_refName;
        std::string * readSeq = kmerPositions->getSequence(readName);
        std::string * refSeq = kmerPositions->getSequence(refName);
        int readLength = readSeq->length();
        int refLength = refSeq->length();

        std::vector<AlignmentLine *> alignmentLines = findAlignmentLines(commonKmerSet, readLength, refLength,
                                                                         verbosity, output,
                                                                         lowScoreThreshold, highScoreThreshold,
                                                                         mergeDistance);

        if (alignmentLines.size() == 0)
            continue;



        for (size_t j = 0; j < alignmentLines.size(); ++j) {
            bool seedChainSuccess = alignmentLines[i]->buildSeedChain(minPointCount, minAlignmentLength);
            if (seedChainSuccess) {
                SemiGlobalAlignment * alignment = semiGlobalAlignmentOneLine(readSeq, refSeq, alignmentLines[i],
                                                                             verbosity, output, scoringScheme);
                if (alignment != 0)
                    alignments.push_back(alignment);
            }
        }

        // Clean up.
        for (size_t i = 0; i < alignmentLines.size(); ++i)
            delete alignmentLines[i];
    }

    return alignments;
}



// char * semiGlobalAlignment(char * readNameC, char * readSeqC, char * refNameC, char * refSeqC,
//                            double expectedSlope, int verbosity, KmerPositions * kmerPositions,
//                            int matchScore, int mismatchScore, int gapOpenScore,
//                            int gapExtensionScore, int sensitivityLevel) {
//     // This string will collect all of the console output for the alignment.
//     std::string output;

//     // Change the read/ref names and sequences to C++ strings.
//     std::string readName(readNameC);
//     std::string refName(refNameC);
//     std::string readSeq(readSeqC);
//     std::string refSeq(refSeqC);
//     int readLength = readSeq.length();
//     int refLength = refSeq.length();

//     // Find all alignment lines in the read-ref rectangle. These will be used as guides for the 
//     // Seqan alignments.
//     LineFindingResults * lineFindingResults = findAlignmentLines(readName, refName,
//                                                                  readLength, refLength,
//                                                                  expectedSlope, verbosity,
//                                                                  kmerPositions, output,
//                                                                  sensitivityLevel);
//     // Now conduct an alignment for each line.
//     std::vector<SemiGlobalAlignment *> alignments;
//     if (lineFindingResults != 0) {
//         Score<int, Simple> scoringScheme(matchScore, mismatchScore, gapExtensionScore, gapOpenScore);
//         for (size_t i = 0; i < lineFindingResults->m_lines.size(); ++i) {
//             AlignmentLine * line = lineFindingResults->m_lines[i];
//             SemiGlobalAlignment * alignment = semiGlobalAlignmentOneLine(readSeq, refSeq, line, verbosity,
//                                                                          output, scoringScheme);
//             if (alignment != 0)
//                 alignments.push_back(alignment);
//         }
//         delete lineFindingResults;
//     }

//     // The returned string is semicolon-delimited. The last part is the console output and the
//     // other parts are alignment description strings.
//     std::string returnString;
//     for (size_t i = 0; i < alignments.size(); ++i) {
//         returnString += alignments[i]->getFullString() + ";";
//         delete alignments[i];
//     }
//     returnString += output;
//     return cppStringToCString(returnString);
// }




 // Runs an alignment using Seqan between one read and one reference along one line.
 // It starts with a smallish band size (fast) and works up to larger ones to see if they improve
 // the alignment.
SemiGlobalAlignment * semiGlobalAlignmentOneLine(std::string * readSeq, std::string * refSeq,
                                                 AlignmentLine * line, int verbosity, std::string & output,
                                                 Score<int, Simple> & scoringScheme) {
    long long startTime = getTime();

    int trimmedRefLength = line->m_trimmedRefEnd - line->m_trimmedRefStart;
    std::string trimmedRefSeq = refSeq->substr(line->m_trimmedRefStart, trimmedRefLength);

    Dna5String readSeqSeqan(*readSeq);
    Dna5String refSeqSeqan(trimmedRefSeq);
    int readLength = readSeq->length();

    int bandSize = STARTING_BAND_SIZE;
    SemiGlobalAlignment * bestAlignment = 0;
    double bestAlignmentScore = std::numeric_limits<double>::min();

    // We perform the alignment with increasing band sizes until the score stops improving or we
    // reach the max band size.
    while (true) {
        SemiGlobalAlignment * alignment = semiGlobalAlignmentOneLineOneBand(readSeqSeqan, readLength, refSeqSeqan, trimmedRefLength,
                                                                            line, bandSize, verbosity, output, scoringScheme);
        if (alignment != 0) {
            double alignmentScore = alignment->m_scaledScore;
            if (alignmentScore <= bestAlignmentScore) {
                delete alignment;
                break;
            }
            else {
                if (bestAlignment != 0)
                    delete bestAlignment;
                bestAlignment = alignment;
                bestAlignmentScore = alignmentScore;
            }
        }    
        bandSize *= 2;
        if (bandSize > MAX_BAND_SIZE)
            break;
    }

    if (bestAlignment != 0)
        bestAlignment->m_milliseconds = getTime() - startTime;
    return bestAlignment;
}






// This function, given a line, will search for semi-global alignments around that line. The
// bandSize parameter specifies how far of an area around the line is searched.
SemiGlobalAlignment * semiGlobalAlignmentOneLineOneBand(Dna5String & readSeq, int readLen,
                                                        Dna5String & refSeq, int refLen,
                                                        AlignmentLine * line, int bandSize,
                                                        int verbosity, std::string & output,
                                                        Score<int, Simple> & scoringScheme) {
    long long startTime = getTime();

    // I encountered a Seqan crash when the band size exceeded the sequence length, so don't let
    // that happen.
    int shortestSeqLen = std::min(readLen, refLen);
    if (bandSize > shortestSeqLen)
        bandSize = shortestSeqLen;

    // The reference sequence here is the trimmed reference sequence, not the whole reference
    // sequence. But the seed chain was made using the same offset as the trimming, so everything
    // should line up nicely (no offset adjustment needed).

    Align<Dna5String, ArrayGaps> alignment;
    resize(rows(alignment), 2);
    assignSource(row(alignment, 0), readSeq);
    assignSource(row(alignment, 1), refSeq);
    AlignConfig<true, true, true, true> alignConfig;

    SemiGlobalAlignment * sgAlignment;
    try {
        bandedChainAlignment(alignment, line->m_bridgedSeedChain, scoringScheme, alignConfig,
                             bandSize);
        sgAlignment = new SemiGlobalAlignment(alignment, line->m_trimmedRefStart, startTime, false, false, scoringScheme);

        if (verbosity > 2)
            output += "  " + sgAlignment->getShortDisplayString() + ", band size = " + std::to_string(bandSize) + "\n";
        if (verbosity > 3)
            output += "    " + sgAlignment->m_cigar + "\n";
    }
    catch (...) {
        if (verbosity > 2)
            output += "  Alignment failed, bandwidth = " + std::to_string(bandSize) + "\n";
        sgAlignment = 0;
    }

    return sgAlignment;
}




// This function is used to conduct a short alignment for the sake of extending a GraphMap
// alignment.
char * startExtensionAlignment(char * read, char * ref,
                               int matchScore, int mismatchScore, int gapOpenScore,
                               int gapExtensionScore) {
    long long startTime = getTime();
    std::string output;

    Dna5String sequenceH = read;
    Dna5String sequenceV = ref;

    Align<Dna5String, ArrayGaps> alignment;
    resize(rows(alignment), 2);
    assignSource(row(alignment, 0), sequenceH);
    assignSource(row(alignment, 1), sequenceV);
    Score<int, Simple> scoringScheme(matchScore, mismatchScore, gapExtensionScore, gapOpenScore);

    // The only free gaps are at the start of ref (the reference sequence).
    AlignConfig<false, true, false, false> alignConfig;
    globalAlignment(alignment, scoringScheme, alignConfig);

    SemiGlobalAlignment startAlignment(alignment, 0, startTime, false, true, scoringScheme);
    return cppStringToCString(startAlignment.getFullString());
}



// This function is used to conduct a short alignment for the sake of extending a GraphMap
// alignment.
char * endExtensionAlignment(char * read, char * ref,
                             int matchScore, int mismatchScore, int gapOpenScore,
                             int gapExtensionScore) {
    long long startTime = getTime();
    std::string output;

    Dna5String sequenceH = read;
    Dna5String sequenceV = ref;

    Align<Dna5String, ArrayGaps> alignment;
    resize(rows(alignment), 2);
    assignSource(row(alignment, 0), sequenceH);
    assignSource(row(alignment, 1), sequenceV);
    Score<int, Simple> scoringScheme(matchScore, mismatchScore, gapExtensionScore, gapOpenScore);

    // The only free gaps are at the end of ref (the reference sequence).
    AlignConfig<false, false, true, false> alignConfig;
    globalAlignment(alignment, scoringScheme, alignConfig);

    SemiGlobalAlignment endAlignment(alignment, 0, startTime, true, false, scoringScheme);
    return cppStringToCString(endAlignment.getFullString());
}



char * cppStringToCString(std::string cpp_string) {
    char * c_string = (char*)malloc(sizeof(char) * (cpp_string.size() + 1));
    std::copy(cpp_string.begin(), cpp_string.end(), c_string);
    c_string[cpp_string.size()] = '\0';
    return c_string;
}




std::string getReverseComplement(std::string sequence)
{
    std::string reverseComplement;
    reverseComplement.reserve(sequence.length());

    for (int i = sequence.length() - 1; i >= 0; --i)
    {
        char letter = sequence[i];
        switch (letter)
        {
        case 'A': reverseComplement.push_back('T'); break;
        case 'T': reverseComplement.push_back('A'); break;
        case 'G': reverseComplement.push_back('C'); break;
        case 'C': reverseComplement.push_back('G'); break;
        case 'R': reverseComplement.push_back('Y'); break;
        case 'Y': reverseComplement.push_back('R'); break;
        case 'S': reverseComplement.push_back('S'); break;
        case 'W': reverseComplement.push_back('W'); break;
        case 'K': reverseComplement.push_back('M'); break;
        case 'M': reverseComplement.push_back('K'); break;
        case 'B': reverseComplement.push_back('V'); break;
        case 'D': reverseComplement.push_back('H'); break;
        case 'H': reverseComplement.push_back('D'); break;
        case 'V': reverseComplement.push_back('B'); break;
        case 'N': reverseComplement.push_back('N'); break;
        case '.': reverseComplement.push_back('.'); break;
        case '-': reverseComplement.push_back('-'); break;
        case '?': reverseComplement.push_back('?'); break;
        case '*': reverseComplement.push_back('*'); break;
        }
    }

    return reverseComplement;
}