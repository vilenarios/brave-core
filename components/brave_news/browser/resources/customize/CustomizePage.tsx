// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import Flex from '$web-common/Flex'
import * as React from 'react'
import styled from 'styled-components'
import { useBraveNews } from '../shared/Context'
import { BackArrow } from '../shared/Icons'
import { getLocale } from '$web-common/locale'
import Button from '$web-components/button'

const BackButtonContainer = styled.div`
  all: unset;
  flex: 1;

  &> button {
    --inner-border-size: 0;
    --outer-border-size: 0;
    padding: 0;

    &:hover {
      --inner-border-size: 0;
      --outer-border-size: 0;
    }
  }
`

const Header = styled.span`
  font-weight: 500;
  font-size: 16px;
  color: var(--text01);
  flex: 5;
  text-align: center;
`

const Spacer = styled.div`flex: 1;`

export default function CustomizePage (props: {
  title: string
  children: React.ReactNode
}) {
  const { setCustomizePage } = useBraveNews()
  return <Flex direction="column">
    <Flex align="center">
      <BackButtonContainer>
        <Button onClick={() => setCustomizePage('news')}>
          {BackArrow}
          {getLocale(S.BRAVE_NEWS_BACK_BUTTON)}
        </Button>
      </BackButtonContainer>
      <Header>{props.title}</Header>
      <Spacer />
    </Flex>
    {props.children}
  </Flex>
}
